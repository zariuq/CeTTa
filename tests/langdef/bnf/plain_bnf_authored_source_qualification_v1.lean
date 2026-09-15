import Mettapedia.GSLT.Parsing.CanonicalSourceOperationalGSLT
import Mettapedia.GSLT.Parsing.HornEquationInstantiation
import Mettapedia.GSLT.Parsing.PlainBnfSourceTextAppend
import Mettapedia.GSLT.Parsing.CanonicalSourceQualification
import Mettapedia.GSLT.Parsing.HornCertificateBoundary
import Mettapedia.GSLT.Parsing.HornIntegerProvider
import Mettapedia.GSLT.Parsing.HornIntegerProviderNativeType
import Mettapedia.GSLT.Parsing.HornProviderGSLT
import Mettapedia.GSLT.Parsing.PlainBnfLexicalScalarSemantics
import Mettapedia.GSLT.Parsing.PlainBnfLexicalInhabitation
import Mettapedia.GSLT.Parsing.PlainBnfSourceScalarCodec
import Mettapedia.GSLT.Parsing.PlainBnfExclusionLocalModel
import Mettapedia.GSLT.Parsing.PlainBnfDeclarationSource
import Mettapedia.GSLT.Parsing.PlainBnfDeclarationRealization
import Mettapedia.GSLT.Parsing.PlainBnfDeclarationReflection
import Mettapedia.GSLT.Parsing.PlainBnfDeclarationOccurrences
import Mathlib

/-!
# Qualification of the authored plain-BNF semantic source composition

This gate reads the exact source files used by the native compiler and expands
them to ordinary Lean constructor data.  It checks the complete six-source
composition rather than a hand-maintained summary or generated target.  The
result authenticates source structure and first-order Horn elaboration; it does
not by itself prove that the admission and graph-analysis rules have their
intended domain meaning.
-/

set_option autoImplicit false

namespace Cetta.PlainBnf.AuthoredSourceQualificationV1

open Algorithms.MeTTa.Simple.Parser (SExpr)
open Mettapedia.OSLF.MeTTaIL.MeTTaSyntaxQuotation
open Mettapedia.GSLT.LanguageDef.CanonicalSourceGSLT
open Mettapedia.GSLT.Parsing.CanonicalSourceHornElaboration
open Mettapedia.GSLT.Parsing.CanonicalSourceOperationalGSLT
open Mettapedia.GSLT.Parsing.CanonicalSourceQualification
open Mettapedia.GSLT.Parsing.HornCertificate
open Mettapedia.GSLT.Parsing.HornCertificateGSLT
open Mettapedia.GSLT.Parsing.HornIntegerProviderNativeType
  (binaryShape binaryQuery binaryCall binary_safe binary_run binary_echo_native_type)
open scoped Mettapedia.OSLF.MeTTaIL.MeTTaSyntaxQuotation

@[simp] private theorem stringToNatZero : "0".toNat? = some 0 := by
  change (Nat.repr 0).toNat? = some 0
  exact Nat.toNat?_repr 0

@[simp] private theorem stringToNatOne : "1".toNat? = some 1 := by
  change (Nat.repr 1).toNat? = some 1
  exact Nat.toNat?_repr 1

@[simp] private theorem stringToNatTwo : "2".toNat? = some 2 := by
  change (Nat.repr 2).toNat? = some 2
  exact Nat.toNat?_repr 2

@[simp] private theorem stringToNatThree : "3".toNat? = some 3 := by
  change (Nat.repr 3).toNat? = some 3
  exact Nat.toNat?_repr 3

@[simp] private theorem stringToNatFour : "4".toNat? = some 4 := by
  change (Nat.repr 4).toNat? = some 4
  exact Nat.toNat?_repr 4

@[simp] private theorem stringToNatFive : "5".toNat? = some 5 := by
  change (Nat.repr 5).toNat? = some 5
  exact Nat.toNat?_repr 5

@[simp] private theorem stringToNatSix : "6".toNat? = some 6 := by
  change (Nat.repr 6).toNat? = some 6
  exact Nat.toNat?_repr 6

@[simp] private theorem stringToNatSeven : "7".toNat? = some 7 := by
  change (Nat.repr 7).toNat? = some 7
  exact Nat.toNat?_repr 7

@[simp] private theorem stringToNatEight : "8".toNat? = some 8 := by
  change (Nat.repr 8).toNat? = some 8
  exact Nat.toNat?_repr 8

@[simp] private theorem stringToIntZero : "0".toInt? = some 0 := by
  change (Nat.repr 0).toInt? = some (Int.ofNat 0)
  exact Nat.toInt?_repr 0

@[simp] private theorem stringToIntOne : "1".toInt? = some 1 := by
  change (Nat.repr 1).toInt? = some (Int.ofNat 1)
  exact Nat.toInt?_repr 1

@[simp] private theorem stringToIntTwo : "2".toInt? = some 2 := by
  change (Nat.repr 2).toInt? = some (Int.ofNat 2)
  exact Nat.toInt?_repr 2

@[simp] private theorem stringToIntThree : "3".toInt? = some 3 := by
  change (Nat.repr 3).toInt? = some (Int.ofNat 3)
  exact Nat.toInt?_repr 3

@[simp] private theorem stringToIntFour : "4".toInt? = some 4 := by
  change (Nat.repr 4).toInt? = some (Int.ofNat 4)
  exact Nat.toInt?_repr 4

@[simp] private theorem stringToIntFive : "5".toInt? = some 5 := by
  change (Nat.repr 5).toInt? = some (Int.ofNat 5)
  exact Nat.toInt?_repr 5

@[simp] private theorem stringToIntSix : "6".toInt? = some 6 := by
  change (Nat.repr 6).toInt? = some (Int.ofNat 6)
  exact Nat.toInt?_repr 6

@[simp] private theorem stringToIntSeven : "7".toInt? = some 7 := by
  change (Nat.repr 7).toInt? = some (Int.ofNat 7)
  exact Nat.toInt?_repr 7

@[simp] private theorem stringToIntEight : "8".toInt? = some 8 := by
  change (Nat.repr 8).toInt? = some (Int.ofNat 8)
  exact Nat.toInt?_repr 8

@[simp] private theorem stringToInt35 : "35".toInt? = some 35 := by
  change (Nat.repr 35).toInt? = some (Int.ofNat 35)
  exact Nat.toInt?_repr 35

@[simp] private theorem stringToInt112 : "112".toInt? = some 112 := by
  change (Nat.repr 112).toInt? = some (Int.ofNat 112)
  exact Nat.toInt?_repr 112

@[simp] private theorem stringToInt120 : "120".toInt? = some 120 := by
  change (Nat.repr 120).toInt? = some (Int.ofNat 120)
  exact Nat.toInt?_repr 120

@[simp] private theorem stringToIntUnicodeMaximum :
    "1114111".toInt? = some 1114111 := by
  change (Nat.repr 1114111).toInt? = some (Int.ofNat 1114111)
  exact Nat.toInt?_repr 1114111

@[simp] private theorem stringToIntSurrogateStart :
    "55296".toInt? = some 55296 := by
  change (Nat.repr 55296).toInt? = some (Int.ofNat 55296)
  exact Nat.toInt?_repr 55296

@[simp] private theorem stringToIntSurrogateStop :
    "57343".toInt? = some 57343 := by
  change (Nat.repr 57343).toInt? = some (Int.ofNat 57343)
  exact Nat.toInt?_repr 57343

@[simp] private theorem intReprZero : Int.repr 0 = "0" := by
  change Nat.repr 0 = Nat.repr 0
  rfl

@[simp] private theorem stringToIntBeforeSurrogate : "55295".toInt? = some 55295 := by
  change (Nat.repr 55295).toInt? = some (Int.ofNat 55295)
  exact Nat.toInt?_repr 55295

@[simp] private theorem stringToIntAfterSurrogate : "57344".toInt? = some 57344 := by
  change (Nat.repr 57344).toInt? = some (Int.ofNat 57344)
  exact Nat.toInt?_repr 57344

@[simp] private theorem intReprBeforeSurrogate : Int.repr 55295 = "55295" := by
  change Nat.repr 55295 = Nat.repr 55295
  rfl

@[simp] private theorem intReprAfterSurrogate : Int.repr 57344 = "57344" := by
  change Nat.repr 57344 = Nat.repr 57344
  rfl

@[simp] private theorem intReprOne : Int.repr 1 = "1" := by
  change Nat.repr 1 = Nat.repr 1
  rfl

@[simp] private theorem intReprTwo : Int.repr 2 = "2" := by
  change Nat.repr 2 = Nat.repr 2
  rfl

@[simp] private theorem intReprThree : Int.repr 3 = "3" := by
  change Nat.repr 3 = Nat.repr 3
  rfl

@[simp] private theorem intReprFour : Int.repr 4 = "4" := by
  change Nat.repr 4 = Nat.repr 4
  rfl

@[simp] private theorem intReprFive : Int.repr 5 = "5" := by
  change Nat.repr 5 = Nat.repr 5
  rfl

@[simp] private theorem intReprSix : Int.repr 6 = "6" := by
  change Nat.repr 6 = Nat.repr 6
  rfl

@[simp] private theorem intReprSeven : Int.repr 7 = "7" := by
  change Nat.repr 7 = Nat.repr 7
  rfl

@[simp] private theorem intReprEight : Int.repr 8 = "8" := by
  change Nat.repr 8 = Nat.repr 8
  rfl

@[simp] private theorem intRepr35 : Int.repr 35 = "35" := by
  change Nat.repr 35 = Nat.repr 35
  rfl

@[simp] private theorem intRepr112 : Int.repr 112 = "112" := by
  change Nat.repr 112 = Nat.repr 112
  rfl

@[simp] private theorem intRepr120 : Int.repr 120 = "120" := by
  change Nat.repr 120 = Nat.repr 120
  rfl

@[simp] private theorem intReprUnicodeMaximum :
    Int.repr 1114111 = "1114111" := by
  change Nat.repr 1114111 = Nat.repr 1114111
  rfl

@[simp] private theorem intReprSurrogateStart :
    Int.repr 55296 = "55296" := by
  change Nat.repr 55296 = Nat.repr 55296
  rfl

@[simp] private theorem intReprSurrogateStop :
    Int.repr 57343 = "57343" := by
  change Nat.repr 57343 = Nat.repr 57343
  rfl

private def groundRelationsProgram : SExpr :=
  metta_sexpr_file% petta
    "../../../experiments/gslt2parse_foundation/presentations/shared/ground_relations_v1.metta"

private def cettaPettaGroundRelationsProgram : SExpr :=
  metta_sexpr_file% petta
    "../../../experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_relations_v1.metta"

private def groundIntegerRelationsProgram : SExpr :=
  metta_sexpr_file% petta
    "../../../experiments/gslt2parse_foundation/presentations/shared/ground_integer_relations_v1.metta"

private def cettaPettaGroundIntegerRelationsProgram : SExpr :=
  metta_sexpr_file% petta
    "../../../experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_integer_relations_v1.metta"

private def semanticAdmissionProgram : SExpr :=
  metta_sexpr_file% petta
    "../../../langdef/bnf/plain_bnf_semantic_admission_v1.metta"

private def graphAnalysisProgram : SExpr :=
  metta_sexpr_file% petta
    "../../../langdef/bnf/plain_bnf_graph_analysis_v1.metta"

/-- The denotation presentation is deployed separately from the six-source
admission/analysis composition, so it is qualified as a separate source. -/
private def denotationProgram : SExpr :=
  metta_sexpr_file% petta
    "../../../langdef/bnf/plain_bnf_denotation_v1.metta"

private def groundRelationsSource : Source :=
  (decode groundRelationsProgram).get (by
    simp [groundRelationsProgram, decode, decodeList,
      decodeOperator, decodeRewrite, atomToken?])

private def cettaPettaGroundRelationsSource : Source :=
  (decode cettaPettaGroundRelationsProgram).get (by
    simp [cettaPettaGroundRelationsProgram, decode, decodeList,
      decodeOperator, decodeRewrite, atomToken?])

private def groundIntegerRelationsSource : Source :=
  (decode groundIntegerRelationsProgram).get (by
    simp [groundIntegerRelationsProgram, decode, decodeList,
      decodeOperator, decodeRewrite, atomToken?])

private def cettaPettaGroundIntegerRelationsSource : Source :=
  (decode cettaPettaGroundIntegerRelationsProgram).get (by
    simp [cettaPettaGroundIntegerRelationsProgram, decode, decodeList, decodeOperator, decodeRewrite, atomToken?])

private def semanticAdmissionSource : Source :=
  (decode semanticAdmissionProgram).get (by
    simp only [semanticAdmissionProgram, decode_fields_isSome_iff,
      List.forall_mem_cons]
    simp [decodeOperator, decodeRewrite, atomToken?])

private def graphAnalysisSource : Source :=
  (decode graphAnalysisProgram).get (by
    simp only [graphAnalysisProgram, decode_fields_isSome_iff,
      List.forall_mem_cons]
    simp [decodeOperator, decodeRewrite, atomToken?])

private def denotationSource : Source :=
  (decode denotationProgram).get (by
    simp only [denotationProgram, decode_fields_isSome_iff,
      List.forall_mem_cons]
    simp [decodeOperator, decodeRewrite, atomToken?])

def authoredSources : List Source :=
  [ groundRelationsSource
  , cettaPettaGroundRelationsSource
  , groundIntegerRelationsSource
  , cettaPettaGroundIntegerRelationsSource
  , semanticAdmissionSource
  , graphAnalysisSource ]

def authoredPrograms : List SExpr :=
  [ groundRelationsProgram
  , cettaPettaGroundRelationsProgram
  , groundIntegerRelationsProgram
  , cettaPettaGroundIntegerRelationsProgram
  , semanticAdmissionProgram
  , graphAnalysisProgram ]

/-- The denotation presentation is an independently deployed source unit. -/
def denotationSources : List Source := [denotationSource]

def decodes : List SExpr -> Option (List Source)
  | [] => some []
  | program :: programs => do
      let source <- decode program
      let sources <- decodes programs
      some (source :: sources)

private theorem groundRelationsProgram_decodes :
    decode groundRelationsProgram = some groundRelationsSource := by
  unfold groundRelationsSource
  exact (Option.some_get _).symm

private theorem cettaPettaGroundRelationsProgram_decodes :
    decode cettaPettaGroundRelationsProgram =
      some cettaPettaGroundRelationsSource := by
  unfold cettaPettaGroundRelationsSource
  exact (Option.some_get _).symm

private theorem groundIntegerRelationsProgram_decodes :
    decode groundIntegerRelationsProgram =
      some groundIntegerRelationsSource := by
  unfold groundIntegerRelationsSource
  exact (Option.some_get _).symm

private theorem cettaPettaGroundIntegerRelationsProgram_decodes :
    decode cettaPettaGroundIntegerRelationsProgram =
      some cettaPettaGroundIntegerRelationsSource := by
  unfold cettaPettaGroundIntegerRelationsSource
  exact (Option.some_get _).symm

private theorem semanticAdmissionProgram_decodes :
    decode semanticAdmissionProgram = some semanticAdmissionSource := by
  unfold semanticAdmissionSource
  exact (Option.some_get _).symm

private theorem graphAnalysisProgram_decodes :
    decode graphAnalysisProgram = some graphAnalysisSource := by
  unfold graphAnalysisSource
  exact (Option.some_get _).symm

private theorem denotationProgram_decodes :
    decode denotationProgram = some denotationSource := by
  unfold denotationSource
  exact (Option.some_get _).symm

/-- The six exact source files, rather than a copied typed fixture, decode to
the source composition qualified below. -/
theorem authoredPrograms_decode_exact :
    decodes authoredPrograms = some authoredSources := by
  simp [decodes, authoredPrograms, authoredSources,
    groundRelationsProgram_decodes,
    cettaPettaGroundRelationsProgram_decodes,
    groundIntegerRelationsProgram_decodes,
    cettaPettaGroundIntegerRelationsProgram_decodes,
    semanticAdmissionProgram_decodes,
    graphAnalysisProgram_decodes]

/-- The exact denotation file decodes to its canonical source object. -/
theorem denotationProgram_decode_exact :
    decodes [denotationProgram] = some denotationSources := by
  simp [decodes, denotationSources, denotationProgram_decodes]
theorem authoredSources_compositionValid :
    compositionValid authoredSources = true := by
  unfold authoredSources
  rw [source_eq_of_decode groundRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundRelationsProgram_decodes,
    source_eq_of_decode groundIntegerRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundIntegerRelationsProgram_decodes,
    source_eq_of_decode semanticAdmissionProgram_decodes,
    source_eq_of_decode graphAnalysisProgram_decodes]
  repeat' (conv in (List.filterMap decodeOperator _) =>
    simp [decodeOperator, atomToken?])
  decide +kernel
/-- The separately deployed denotation presentation is a valid closed source
composition.  This theorem authenticates its schema, not its intended fold. -/
theorem denotationSource_compositionValid :
    compositionValid denotationSources = true := by
  unfold denotationSources
  rw [source_eq_of_decode denotationProgram_decodes]
  simp
    [compositionValid, decodeOperator, decodeRewrite, atomToken?, compositionOperators, compositionRewriteNames, Source.hasValidSchema, Source.termsValidIn, Rewrite.hasValidShape, rewriteName, termSupported, termsSupported, hasOperator]

/-- Decode and admit the same closed composition passed to the native
compiler.  Failure has no default source or empty-program recovery. -/
def admittedSources? : Option (List Source) := do
  if compositionValid authoredSources then some authoredSources else none

structure SourceInventory where
  sourceNames : List String
  operatorCounts : List Nat
  equationCounts : List Nat
  rewriteCounts : List Nat
  totalOperators : Nat
  totalRewrites : Nat
  deriving DecidableEq, Repr

def inventory (sources : List Source) : SourceInventory :=
  { sourceNames := sources.map (·.name)
    operatorCounts := sources.map (·.operators.length)
    equationCounts := sources.map (·.equations.length)
    rewriteCounts := sources.map (·.rewrites.length)
    totalOperators := (compositionOperators sources).length
    totalRewrites := (compositionRewriteNames sources).length }

def admittedInventory? : Option SourceInventory :=
  inventory <$> admittedSources?
/-- The literal source files decode canonically and form the complete closed
composition expected by the native generator. -/
theorem admitted_inventory_exact :
    admittedInventory? = some {
      sourceNames :=
        [ "GroundRelationsV1"
        , "CettaPeTTaGroundRelationsV1"
        , "GroundIntegerRelationsV1"
        , "CettaPeTTaGroundIntegerRelationsV1"
        , "PlainBnfSemanticAdmissionV1"
        , "PlainBnfGraphAnalysisV1" ]
      operatorCounts := [2, 7, 7, 19, 78, 78]
      equationCounts := [0, 0, 0, 0, 0, 0]
      rewriteCounts := [1, 1, 6, 6, 67, 93]
      totalOperators := 191
      totalRewrites := 174 } := by
  rw [admittedInventory?, admittedSources?, authoredSources_compositionValid]
  change some (inventory authoredSources) = some _
  apply congrArg some
  have fields0 := source_field_lengths groundRelationsProgram_decodes
  have fields1 := source_field_lengths cettaPettaGroundRelationsProgram_decodes
  have fields2 := source_field_lengths groundIntegerRelationsProgram_decodes
  have fields3 := source_field_lengths cettaPettaGroundIntegerRelationsProgram_decodes
  have fields4 := source_field_lengths semanticAdmissionProgram_decodes
  have fields5 := source_field_lengths graphAnalysisProgram_decodes
  simp only [inventory, compositionOperators, compositionRewriteNames,
    List.length_flatMap, List.length_map]
  simp only [authoredSources, List.map_cons, List.map_nil,
    fields0.1,
    fields0.2.1,
    fields0.2.2.1,
    fields0.2.2.2,
    fields1.1,
    fields1.2.1,
    fields1.2.2.1,
    fields1.2.2.2,
    fields2.1,
    fields2.2.1,
    fields2.2.2.1,
    fields2.2.2.2,
    fields3.1,
    fields3.2.1,
    fields3.2.2.1,
    fields3.2.2.2,
    fields4.1,
    fields4.2.1,
    fields4.2.2.1,
    fields4.2.2.2,
    fields5.1,
    fields5.2.1,
    fields5.2.2.1,
    fields5.2.2.2]
  decide +kernel

theorem compositionRewriteNames_length (sources : List Source) :
    (compositionRewriteNames sources).length =
      (compositionRewrites sources).length := by
  induction sources with
  | nil => rfl
  | cons source sources _ =>
      simp [compositionRewriteNames, compositionRewrites]
private theorem groundRelationsSource_elaborates :
    (elaborateRewrites? groundRelationsSource.rewrites).isSome = true := by
  rw [source_eq_of_decode groundRelationsProgram_decodes]
  simp
    [decodeRewrite, atomToken?, elaborateRewrites?, elaborateRewrite?, elaborateRewriteCandidate?, rewriteVariableNames, termVariableOccurrences, termsVariableOccurrences, elaborateAtom?, elaborateAtoms?, elaborateTerm?, elaborateTerms?, quoteRule?, quoteAtom?, quoteAtoms?, quoteTerm?, quoteTerms?, sourceVariableToken, mayStartInteger, distinctNames, collectDistinctNames]
private theorem cettaPettaGroundRelationsSource_elaborates :
    (elaborateRewrites?
      cettaPettaGroundRelationsSource.rewrites).isSome = true := by
  rw [source_eq_of_decode cettaPettaGroundRelationsProgram_decodes]
  simp
    [decodeRewrite, atomToken?, elaborateRewrites?, elaborateRewrite?, elaborateRewriteCandidate?, rewriteVariableNames, termVariableOccurrences, termsVariableOccurrences, elaborateAtom?, elaborateAtoms?, elaborateTerm?, elaborateTerms?, quoteRule?, quoteAtom?, quoteAtoms?, quoteTerm?, quoteTerms?, sourceVariableToken, mayStartInteger, distinctNames, collectDistinctNames, nameIndex?, nameIndexFrom?]
private theorem groundIntegerRelationsSource_elaborates :
    (elaborateRewrites?
      groundIntegerRelationsSource.rewrites).isSome = true := by
  rw [source_eq_of_decode groundIntegerRelationsProgram_decodes]
  simp
    [decodeRewrite, atomToken?, elaborateRewrites?, elaborateRewrite?, elaborateRewriteCandidate?, rewriteVariableNames, termVariableOccurrences, termsVariableOccurrences, elaborateAtom?, elaborateAtoms?, elaborateTerm?, elaborateTerms?, quoteRule?, quoteAtom?, quoteAtoms?, quoteTerm?, quoteTerms?, sourceVariableToken, mayStartInteger, distinctNames, collectDistinctNames]
private theorem cettaPettaGroundIntegerRelationsSource_elaborates :
    (elaborateRewrites?
      cettaPettaGroundIntegerRelationsSource.rewrites).isSome = true := by
  rw [source_eq_of_decode cettaPettaGroundIntegerRelationsProgram_decodes]
  simp
    [decodeRewrite, atomToken?, elaborateRewrites?, elaborateRewrite?, elaborateRewriteCandidate?, rewriteVariableNames, termVariableOccurrences, termsVariableOccurrences, elaborateAtom?, elaborateAtoms?, elaborateTerm?, elaborateTerms?, quoteRule?, quoteAtom?, quoteAtoms?, quoteTerm?, quoteTerms?, sourceVariableToken, mayStartInteger, distinctNames, collectDistinctNames, nameIndex?, nameIndexFrom?]
/-- Proof chunks inspect actual source occurrences; the final indexed theorem
checks that their concatenation covers the complete decoded inventory. -/
local macro "qualify_authored_rows " raw:ident : tactic =>
  `(tactic| first | decide +kernel | (
    simp only [List.all_cons, List.all_nil, Bool.and_eq_true, and_true]
    repeat' constructor
    all_goals dsimp only [$raw:term, rawRewriteAt?, List.getElem?_cons_succ,
      List.getElem?_cons_zero, Option.bind_some]
    all_goals simp [decodeRewrite, atomToken?, elaborateRewrite?, elaborateRewriteCandidate?,
      rewriteVariableNames, termVariableOccurrences, termsVariableOccurrences, elaborateAtom?,
      elaborateAtoms?, elaborateTerm?, elaborateTerms?, quoteRule?, quoteAtom?, quoteAtoms?,
      quoteTerm?, quoteTerms?, sourceVariableToken, mayStartInteger, distinctNames,
      collectDistinctNames, nameIndex?, nameIndexFrom?]))

private theorem semanticAdmissionRows_0 :
    [0, 1, 2, 3, 4, 5].all (fun index =>
      ((rawRewriteAt? semanticAdmissionProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows semanticAdmissionProgram

private theorem semanticAdmissionRows_6 :
    [6, 7, 8, 9, 10, 11].all (fun index =>
      ((rawRewriteAt? semanticAdmissionProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows semanticAdmissionProgram

private theorem semanticAdmissionRows_12 :
    [12, 13, 14, 15, 16, 17].all (fun index =>
      ((rawRewriteAt? semanticAdmissionProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows semanticAdmissionProgram

private theorem semanticAdmissionRows_18 :
    [18, 19, 20, 21, 22, 23].all (fun index =>
      ((rawRewriteAt? semanticAdmissionProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows semanticAdmissionProgram

private theorem semanticAdmissionRows_24 :
    [24, 25, 26, 27, 28, 29].all (fun index =>
      ((rawRewriteAt? semanticAdmissionProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows semanticAdmissionProgram

private theorem semanticAdmissionRows_30 :
    [30, 31, 32, 33, 34, 35].all (fun index =>
      ((rawRewriteAt? semanticAdmissionProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows semanticAdmissionProgram

private theorem semanticAdmissionRows_36 :
    [36, 37, 38, 39, 40, 41].all (fun index =>
      ((rawRewriteAt? semanticAdmissionProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows semanticAdmissionProgram

private theorem semanticAdmissionRows_42 :
    [42, 43, 44, 45, 46, 47].all (fun index =>
      ((rawRewriteAt? semanticAdmissionProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows semanticAdmissionProgram

private theorem semanticAdmissionRows_48 :
    [48, 49, 50, 51, 52, 53].all (fun index =>
      ((rawRewriteAt? semanticAdmissionProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows semanticAdmissionProgram

private theorem semanticAdmissionRows_54 :
    [54, 55, 56, 57, 58, 59].all (fun index =>
      ((rawRewriteAt? semanticAdmissionProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows semanticAdmissionProgram

private theorem semanticAdmissionRows_60 :
    [60, 61, 62, 63, 64, 65].all (fun index =>
      ((rawRewriteAt? semanticAdmissionProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows semanticAdmissionProgram

private theorem semanticAdmissionRows_66 :
    [66].all (fun index =>
      ((rawRewriteAt? semanticAdmissionProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows semanticAdmissionProgram

private theorem semanticAdmissionSource_elaborates :
    (elaborateRewrites? semanticAdmissionSource.rewrites).isSome = true := by
  rw [elaborateRewrites_isSome_iff_indices]
  have count : semanticAdmissionSource.rewrites.length = 67 := by
    rw [source_eq_of_decode semanticAdmissionProgram_decodes]
    decide +kernel
  rw [count]
  simp only [rawRewriteAt?_of_decode semanticAdmissionProgram_decodes]
  rw [show List.range 67 =
    [0, 1, 2, 3, 4, 5] ++
    [6, 7, 8, 9, 10, 11] ++
    [12, 13, 14, 15, 16, 17] ++
    [18, 19, 20, 21, 22, 23] ++
    [24, 25, 26, 27, 28, 29] ++
    [30, 31, 32, 33, 34, 35] ++
    [36, 37, 38, 39, 40, 41] ++
    [42, 43, 44, 45, 46, 47] ++
    [48, 49, 50, 51, 52, 53] ++
    [54, 55, 56, 57, 58, 59] ++
    [60, 61, 62, 63, 64, 65] ++
    [66] by decide +kernel]
  simp only [List.all_append,
    semanticAdmissionRows_0,
    semanticAdmissionRows_6,
    semanticAdmissionRows_12,
    semanticAdmissionRows_18,
    semanticAdmissionRows_24,
    semanticAdmissionRows_30,
    semanticAdmissionRows_36,
    semanticAdmissionRows_42,
    semanticAdmissionRows_48,
    semanticAdmissionRows_54,
    semanticAdmissionRows_60,
    semanticAdmissionRows_66, Bool.and_self]

private theorem graphAnalysisRows_0 :
    [0, 1, 2, 3, 4, 5].all (fun index =>
      ((rawRewriteAt? graphAnalysisProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows graphAnalysisProgram

private theorem graphAnalysisRows_6 :
    [6, 7, 8, 9, 10, 11].all (fun index =>
      ((rawRewriteAt? graphAnalysisProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows graphAnalysisProgram

private theorem graphAnalysisRows_12 :
    [12, 13, 14, 15, 16, 17].all (fun index =>
      ((rawRewriteAt? graphAnalysisProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows graphAnalysisProgram

private theorem graphAnalysisRows_18 :
    [18, 19, 20, 21, 22, 23].all (fun index =>
      ((rawRewriteAt? graphAnalysisProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows graphAnalysisProgram

private theorem graphAnalysisRows_24 :
    [24, 25, 26, 27, 28, 29].all (fun index =>
      ((rawRewriteAt? graphAnalysisProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows graphAnalysisProgram

private theorem graphAnalysisRows_30 :
    [30, 31, 32, 33, 34, 35].all (fun index =>
      ((rawRewriteAt? graphAnalysisProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows graphAnalysisProgram

private theorem graphAnalysisRows_36 :
    [36, 37, 38, 39, 40, 41].all (fun index =>
      ((rawRewriteAt? graphAnalysisProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows graphAnalysisProgram

private theorem graphAnalysisRows_42 :
    [42, 43, 44, 45, 46, 47].all (fun index =>
      ((rawRewriteAt? graphAnalysisProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows graphAnalysisProgram

private theorem graphAnalysisRows_48 :
    [48, 49, 50, 51, 52, 53].all (fun index =>
      ((rawRewriteAt? graphAnalysisProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows graphAnalysisProgram

private theorem graphAnalysisRows_54 :
    [54, 55, 56, 57, 58, 59].all (fun index =>
      ((rawRewriteAt? graphAnalysisProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows graphAnalysisProgram

private theorem graphAnalysisRows_60 :
    [60, 61, 62, 63, 64, 65].all (fun index =>
      ((rawRewriteAt? graphAnalysisProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows graphAnalysisProgram

private theorem graphAnalysisRows_66 :
    [66, 67, 68, 69, 70, 71].all (fun index =>
      ((rawRewriteAt? graphAnalysisProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows graphAnalysisProgram

private theorem graphAnalysisRows_72 :
    [72, 73, 74, 75, 76, 77].all (fun index =>
      ((rawRewriteAt? graphAnalysisProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows graphAnalysisProgram

private theorem graphAnalysisRows_78 :
    [78, 79, 80, 81, 82, 83].all (fun index =>
      ((rawRewriteAt? graphAnalysisProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows graphAnalysisProgram

private theorem graphAnalysisRows_84 :
    [84, 85, 86, 87, 88, 89].all (fun index =>
      ((rawRewriteAt? graphAnalysisProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows graphAnalysisProgram

private theorem graphAnalysisRows_90 :
    [90, 91, 92].all (fun index =>
      ((rawRewriteAt? graphAnalysisProgram index).bind elaborateRewrite?).isSome) = true := by
  qualify_authored_rows graphAnalysisProgram

private theorem graphAnalysisSource_elaborates :
    (elaborateRewrites? graphAnalysisSource.rewrites).isSome = true := by
  rw [elaborateRewrites_isSome_iff_indices]
  have count : graphAnalysisSource.rewrites.length = 93 := by
    rw [source_eq_of_decode graphAnalysisProgram_decodes]
    decide +kernel
  rw [count]
  simp only [rawRewriteAt?_of_decode graphAnalysisProgram_decodes]
  rw [show List.range 93 =
    [0, 1, 2, 3, 4, 5] ++
    [6, 7, 8, 9, 10, 11] ++
    [12, 13, 14, 15, 16, 17] ++
    [18, 19, 20, 21, 22, 23] ++
    [24, 25, 26, 27, 28, 29] ++
    [30, 31, 32, 33, 34, 35] ++
    [36, 37, 38, 39, 40, 41] ++
    [42, 43, 44, 45, 46, 47] ++
    [48, 49, 50, 51, 52, 53] ++
    [54, 55, 56, 57, 58, 59] ++
    [60, 61, 62, 63, 64, 65] ++
    [66, 67, 68, 69, 70, 71] ++
    [72, 73, 74, 75, 76, 77] ++
    [78, 79, 80, 81, 82, 83] ++
    [84, 85, 86, 87, 88, 89] ++
    [90, 91, 92] by decide +kernel]
  simp only [List.all_append,
    graphAnalysisRows_0,
    graphAnalysisRows_6,
    graphAnalysisRows_12,
    graphAnalysisRows_18,
    graphAnalysisRows_24,
    graphAnalysisRows_30,
    graphAnalysisRows_36,
    graphAnalysisRows_42,
    graphAnalysisRows_48,
    graphAnalysisRows_54,
    graphAnalysisRows_60,
    graphAnalysisRows_66,
    graphAnalysisRows_72,
    graphAnalysisRows_78,
    graphAnalysisRows_84,
    graphAnalysisRows_90, Bool.and_self]

private theorem denotationSource_elaborates :
    (elaborateRewrites? denotationSource.rewrites).isSome = true := by
  rw [source_eq_of_decode denotationProgram_decodes]
  simp
    [decodeRewrite, atomToken?, elaborateRewrites?, elaborateRewrite?, elaborateRewriteCandidate?, rewriteVariableNames, termVariableOccurrences, termsVariableOccurrences, elaborateAtom?, elaborateAtoms?, elaborateTerm?, elaborateTerms?, quoteRule?, quoteAtom?, quoteAtoms?, quoteTerm?, quoteTerms?, sourceVariableToken, mayStartInteger, distinctNames, collectDistinctNames, nameIndex?, nameIndexFrom?]

private theorem compositionRewrites_elaborate :
    (elaborateRewrites? (compositionRewrites authoredSources)).isSome = true := by
  simp [authoredSources, compositionRewrites,
    elaborateRewrites?_append_isSome,
    groundRelationsSource_elaborates,
    cettaPettaGroundRelationsSource_elaborates,
    groundIntegerRelationsSource_elaborates,
    cettaPettaGroundIntegerRelationsSource_elaborates,
    semanticAdmissionSource_elaborates,
    graphAnalysisSource_elaborates]
private theorem authoredSources_noEquations :
    authoredSources.all (fun source => source.equations.isEmpty) = true := by
  unfold authoredSources
  rw [source_eq_of_decode groundRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundRelationsProgram_decodes,
    source_eq_of_decode groundIntegerRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundIntegerRelationsProgram_decodes,
    source_eq_of_decode semanticAdmissionProgram_decodes,
    source_eq_of_decode graphAnalysisProgram_decodes]
  rfl

/-- The exact six authored source programs elaborate to one ordered Horn
program.  The proof is compositional over the quoted source files. -/
theorem authoredSources_elaborate :
    (elaborateProgram? authoredSources).isSome = true := by
  simp [elaborateProgram?, elaborateComposition?,
    authoredSources_compositionValid, authoredSources_noEquations,
    compositionRewrites_elaborate]

/-- The unique ordered Horn program produced from the exact quoted sources. -/
def authoredProgram : Mettapedia.GSLT.Parsing.HornCertificate.Program :=
  (elaborateProgram? authoredSources).get (by
    exact authoredSources_elaborate)

/-- `authoredProgram` is obtained by executing the canonical elaborator on the
quoted files, not by restating its rules. -/
theorem authoredProgram_source_exact :
    elaborateProgram? authoredSources = some authoredProgram := by
  unfold authoredProgram
  exact (Option.some_get _).symm

/-- Exact clause-only semantics of the six-source composition. External
capability declarations and emitted target equations remain metadata in this
interpretation; their PeTTa evaluation is not asserted by this theorem. -/
theorem authoredProgram_operational_correspondence
    (fuel : Nat) (goal : GroundAtom) :
    elaborateTheory? authoredSources = some (theory authoredProgram) /\
      ((∃ certificate,
          replay authoredProgram fuel goal certificate = true) ↔
        (theory authoredProgram).MultiStep [(fuel, goal)] []) := by
  exact source_program_operational_correspondence
    authoredProgram_source_exact fuel goal

/-- The exact denotation source elaborates to an ordered Horn inventory of
its authored equation rules.  This is intentionally separate from proving
that recursive evaluator execution computes the typed denotation fold. -/
theorem denotationSources_elaborate :
    (elaborateProgram? denotationSources).isSome = true := by
  have noEquations :
      denotationSources.all (fun source => source.equations.isEmpty) = true := by
    unfold denotationSources
    rw [source_eq_of_decode denotationProgram_decodes]
    rfl
  unfold elaborateProgram? elaborateComposition?
  rw [denotationSource_compositionValid, noEquations]
  simpa [denotationSources, compositionRewrites] using
    denotationSource_elaborates

/-- The ordered Horn inventory obtained from the exact denotation source. -/
def denotationHornProgram :
    Mettapedia.GSLT.Parsing.HornCertificate.Program :=
  (elaborateProgram? denotationSources).get (by
    exact denotationSources_elaborate)

/-- The denotation Horn inventory is computed from the quoted source. -/
theorem denotationHornProgram_source_exact :
    elaborateProgram? denotationSources = some denotationHornProgram := by
  unfold denotationHornProgram
  exact (Option.some_get _).symm

/-- Bounded selection and instantiation of the 48 authored denotation rules
agrees exactly with the corresponding operational Horn GSLT.  This theorem
does not identify that rule-selection semantics with recursive evaluator
execution. -/
theorem denotationRuleSelection_operational_correspondence
    (fuel : Nat) (goal : GroundAtom) :
    elaborateTheory? denotationSources = some (theory denotationHornProgram) /\
      ((∃ certificate,
          replay denotationHornProgram fuel goal certificate = true) ↔
        (theory denotationHornProgram).MultiStep [(fuel, goal)] []) := by
  exact source_program_operational_correspondence
    denotationHornProgram_source_exact fuel goal
/-- Exact source occurrence count for the separately deployed denotation
presentation. -/
theorem denotationHornProgram_rule_count_exact :
    denotationHornProgram.length = 48 := by
  have preserved :=
    elaborateProgram?_preserves_length denotationHornProgram_source_exact
  have sourceCount : denotationSource.rewrites.length = 48 := by
    rw [source_eq_of_decode denotationProgram_decodes]
    simp [decodeRewrite, atomToken?]
  rw [preserved]
  simpa [denotationSources, compositionRewrites] using sourceCount

/-- Select the actual authored suffix rule and inspect its elaborated
arguments.  Selection here is a source inspection, not evaluator dispatch. -/
def denotationSuffixArguments : Option Terms := do
  let sourceRule ← denotationSource.rewrites.find? (fun rule => rule.name == "suffix-start")
  let elaborated ← elaborateRewrite? sourceRule
  some elaborated.2.head.arguments
/-- The actual file preserves both the nullary call and the nullary text
constructor.  Replacing either with a symbol changes this result. -/
theorem denotation_source_suffix_arguments_exact :
    denotationSuffixArguments = some
      (.cons (.app "bnf-v1:suffix-start" .nil)
        (.cons (.app "bnf-v1:text-cons"
          (.cons (.integer 35) (.cons (.app "bnf-v1:text-nil" .nil) .nil))) .nil)) := by
  unfold denotationSuffixArguments
  rw [source_eq_of_decode denotationProgram_decodes]
  simp
    [decodeRewrite, atomToken?, elaborateRewrite?, elaborateRewriteCandidate?, rewriteVariableNames, termVariableOccurrences, termsVariableOccurrences, elaborateAtom?, elaborateAtoms?, elaborateTerms?, elaborateTerm?, quoteRule?, quoteAtom?, quoteAtoms?, quoteTerms?, quoteTerm?, sourceVariableToken, mayStartInteger, distinctNames, collectDistinctNames]

/-- Exercise each actual source row with a ground left-side instance.
This is a source-instantiation control, not an evaluated-denotation test. -/
def denotationRowInstanceChecks : List Bool :=
  denotationHornProgram.zipIdx.map fun (rule, index) =>
    match Mettapedia.GSLT.Parsing.HornEquationInstantiation.equationSides? rule with
    | none => false
    | some (left, _) =>
        let identifiers := Mettapedia.GSLT.Parsing.HornSpecialization.termVariables left
        let substitution := identifiers.map fun identifier =>
          (identifier, GroundTerm.atom s!"ground-{identifier}")
        match instantiateTerm substitution left with
        | none => false
        | some call =>
            match Mettapedia.GSLT.Parsing.HornEquationInstantiation.instantiateEquationAt?
                denotationHornProgram index call with
            | none => false
            | some (residual, certificate) =>
                replay denotationHornProgram 1
                  (Mettapedia.GSLT.Parsing.HornEquationInstantiation.equationGoal call residual)
                  certificate

#guard denotationRowInstanceChecks.length == 48 && denotationRowInstanceChecks.all id

/-- Exercise the real source against the range-safety premise of the general
completeness theorem. This check does not turn a residual into a value. -/
def denotationRowRangeChecks : List Bool :=
  denotationHornProgram.map fun rule =>
    match Mettapedia.GSLT.Parsing.HornEquationInstantiation.equationSides? rule with
    | none => false
    | some (left, right) =>
        (Mettapedia.GSLT.Parsing.HornSpecialization.termVariables right).all fun identifier =>
          identifier ∈ Mettapedia.GSLT.Parsing.HornSpecialization.termVariables left

#guard denotationRowRangeChecks.length == 48 && denotationRowRangeChecks.all id

/-- Every residual computed at any actual authored occurrence is certified
against the same source program. The residual is not yet a final value. -/
theorem denotation_residual_source_certified
    (occurrence : Nat) (call residual : GroundTerm) (certificate : Certificate)
    (produced : Mettapedia.GSLT.Parsing.HornEquationInstantiation.instantiateEquationAt?
      denotationHornProgram occurrence call = some (residual, certificate)) :
    replay denotationHornProgram 1
      (Mettapedia.GSLT.Parsing.HornEquationInstantiation.equationGoal call residual)
      certificate = true :=
  Mettapedia.GSLT.Parsing.HornEquationInstantiation.instantiateEquationAt?_replays produced

#print axioms denotation_residual_source_certified

open Mettapedia.GSLT.Parsing.HornEquationInstantiation
open Mettapedia.GSLT.Parsing.HornEquationContextual
open Mettapedia.GSLT.Parsing.PlainBnfSourceTextAppend

theorem denotation_append_empty_source :
    (denotationHornProgram[39]?).bind equationSides? = some emptyShape := by
  rw [elaborateProgram?_getElem? denotationHornProgram_source_exact]
  simp only [compositionRewrites, denotationSources, List.flatMap_cons,
    List.flatMap_nil, List.append_nil]
  rw [rawRewriteAt?_of_decode denotationProgram_decodes]
  dsimp only [denotationProgram, rawRewriteAt?, List.getElem?_cons_succ,
    List.getElem?_cons_zero, Option.bind_some, decodeRewrite]
  decide +kernel

theorem denotation_append_cons_source :
    (denotationHornProgram[40]?).bind equationSides? = some consShape := by
  rw [elaborateProgram?_getElem? denotationHornProgram_source_exact]
  simp only [compositionRewrites, denotationSources, List.flatMap_cons,
    List.flatMap_nil, List.append_nil]
  rw [rawRewriteAt?_of_decode denotationProgram_decodes]
  dsimp only [denotationProgram, rawRewriteAt?, List.getElem?_cons_succ,
    List.getElem?_cons_zero, Option.bind_some, decodeRewrite]
  decide +kernel

/-- For arbitrary text, the recursive contextual path of the actual authored
source computes ordinary concatenation and retains every character occurrence.
This is not a theorem about the native evaluator's scheduling. -/
theorem denotation_text_append_all_inputs (left right : List Char) :
    replayPath denotationHornProgram (appendActions 39 40 left.length)
      (appendCall left right) = some (encodeText (left ++ right)) :=
  append_replay denotation_append_empty_source
    denotation_append_cons_source left right

theorem denotation_text_append_path_iff (left right : List Char) (result : GroundTerm) :
    Path denotationHornProgram (appendActions 39 40 left.length)
      (appendCall left right) result ↔ result = encodeText (left ++ right) :=
  append_path_iff denotation_append_empty_source denotation_append_cons_source left right result

#guard replayPath denotationHornProgram [⟨39, []⟩] (appendCall ['a'] []) == none
#guard replayPath denotationHornProgram [⟨40, [0]⟩] (appendCall ['a'] []) == none
#guard (appendActions 39 40 2).map Action.occurrence == [40, 40, 39]
#guard replayPath denotationHornProgram (appendActions 39 40 3)
  (appendCall ['a', 'a', Char.ofNat 0] ['b']) ==
    some (encodeText ['a', 'a', Char.ofNat 0, 'b'])

#print axioms denotation_append_empty_source
#print axioms denotation_append_cons_source
#print axioms denotation_text_append_all_inputs
#print axioms denotation_text_append_path_iff

/-- Every accepted Horn elaboration of the exact six-source composition has
all 174 directed rule occurrences, in source order.  This is deliberately
conditional on elaboration success; source inventory cannot prove that every
rule lies in the first-order Horn fragment. -/
theorem admitted_horn_rule_count_exact
    {program : Mettapedia.GSLT.Parsing.HornCertificate.Program}
    (accepted : admittedSources?.bind elaborateProgram? = some program) :
    program.length = 174 := by
  have accepted' : elaborateProgram? authoredSources = some program := by
    simpa [admittedSources?, authoredSources_compositionValid] using accepted
  have preserved := elaborateProgram?_preserves_length accepted'
  have inventoryEquality :
      inventory authoredSources = {
        sourceNames :=
          [ "GroundRelationsV1"
          , "CettaPeTTaGroundRelationsV1"
          , "GroundIntegerRelationsV1"
          , "CettaPeTTaGroundIntegerRelationsV1"
          , "PlainBnfSemanticAdmissionV1"
          , "PlainBnfGraphAnalysisV1" ]
        operatorCounts := [2, 7, 7, 19, 78, 78]
        equationCounts := [0, 0, 0, 0, 0, 0]
        rewriteCounts := [1, 1, 6, 6, 67, 93]
        totalOperators := 191
        totalRewrites := 174 } := by
    simpa [admittedInventory?, admittedSources?,
      authoredSources_compositionValid] using admitted_inventory_exact
  have rewriteNameCount :
      (compositionRewriteNames authoredSources).length = 174 := by
    have fieldEquality := congrArg SourceInventory.totalRewrites inventoryEquality
    simpa [inventory] using fieldEquality
  rw [preserved, <- compositionRewriteNames_length]
  exact rewriteNameCount

theorem authoredProgram_rule_count_exact : authoredProgram.length = 174 := by
  apply admitted_horn_rule_count_exact
  simpa [admittedSources?, authoredSources_compositionValid] using
    authoredProgram_source_exact

/-- These relations form an unseeded dependency set in the pure Horn
inventory. This does not say that a native primitive provider is absent. -/
def unseededSourceRelations : List String :=
  ["different", "ground-integer-less", "ground-integer-not-less",
    "ground-integer-gap", "ground-integer-no-gap",
    "BNFExclusionTailInhabitedV1", "BNFExclusionGapInhabitedV1"]

theorem authoredSources_unseeded_check :
    Mettapedia.GSLT.Parsing.HornCertificateBoundary.checkSourceUnseeded
      authoredSources unseededSourceRelations = true := by
  unfold authoredSources
  rw [source_eq_of_decode groundRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundRelationsProgram_decodes,
    source_eq_of_decode groundIntegerRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundIntegerRelationsProgram_decodes,
    source_eq_of_decode semanticAdmissionProgram_decodes,
    source_eq_of_decode graphAnalysisProgram_decodes]
  decide +kernel

#print axioms authoredSources_unseeded_check

open Mettapedia.GSLT.Parsing.HornCertificateBoundary

theorem authoredProgram_unseeded :
    Unseeded authoredProgram unseededSourceRelations :=
  unseeded_of_source_check authoredProgram_source_exact
    unseededSourceRelations authoredSources_unseeded_check

/-- The obstruction applies to every argument tuple and every finite proof
depth. It is not a timeout or a bounded search result. -/
theorem authoredProgram_unseeded_no_derivation
    (relation : String) (blocked : relation ∈ unseededSourceRelations)
    (arguments : GroundTerms) (fuel : Nat) :
    ¬ DerivesWithin authoredProgram fuel ⟨relation, arguments⟩ :=
  authoredProgram_unseeded.not_derivable fuel _ blocked

/-- The gap submachine cannot execute to an answer in the clause-only model,
even though generated PeTTa can answer through its arithmetic providers. -/
theorem exclusionGap_has_no_clause_only_terminal_path
    (arguments : GroundTerms) (fuel : Nat) :
    ¬ (theory authoredProgram).MultiStep
      [(fuel, ⟨"BNFExclusionGapInhabitedV1", arguments⟩)] [] :=
  authoredProgram_unseeded.no_terminal_path fuel _ (by simp [unseededSourceRelations])

theorem exclusionTail_has_no_clause_only_certificate
    (arguments : GroundTerms) (fuel : Nat) (certificate : Certificate) :
    replay authoredProgram fuel ⟨"BNFExclusionTailInhabitedV1", arguments⟩
      certificate = false :=
  authoredProgram_unseeded.no_certificate fuel _
    (by simp [unseededSourceRelations]) certificate

#print axioms authoredProgram_unseeded
#print axioms authoredProgram_unseeded_no_derivation
#print axioms exclusionGap_has_no_clause_only_terminal_path
#print axioms exclusionTail_has_no_clause_only_certificate

/-- Negative control: the four relation-provider sources contain only fourteen
rules.  Omitting the two domain sources therefore cannot yield the admitted
174-rule program. -/
theorem provider_only_rewrite_count_exact :
    (compositionRewriteNames (authoredSources.take 4)).length = 14 := by
  unfold authoredSources
  rw [source_eq_of_decode groundRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundRelationsProgram_decodes,
    source_eq_of_decode groundIntegerRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundIntegerRelationsProgram_decodes]
  simp
    [decodeOperator, decodeRewrite, atomToken?, compositionRewriteNames]

#print axioms admitted_inventory_exact
#print axioms authoredPrograms_decode_exact
#print axioms denotationProgram_decode_exact
#print axioms authoredSources_elaborate
#print axioms authoredProgram_source_exact
#print axioms authoredProgram_operational_correspondence
#print axioms denotationSource_compositionValid
#print axioms denotationSources_elaborate
#print axioms denotationHornProgram_source_exact
#print axioms denotationRuleSelection_operational_correspondence
#print axioms denotationHornProgram_rule_count_exact
#print axioms denotation_source_suffix_arguments_exact
#print axioms admitted_horn_rule_count_exact
#print axioms authoredProgram_rule_count_exact
#print axioms provider_only_rewrite_count_exact

namespace IntegerProviders

open Mettapedia.GSLT.Parsing
open Mettapedia.GSLT.Parsing.HornIntegerProvider

theorem less_source :
    (authoredProgram[8]?).bind equationSides? =
      some (binaryShape "ground-integer-less" "<" false) := by
  rw [elaborateProgram?_getElem? authoredProgram_source_exact]
  simp only [compositionRewrites, authoredSources, List.flatMap_cons, List.flatMap_nil]
  rw [source_eq_of_decode groundRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundRelationsProgram_decodes,
    source_eq_of_decode groundIntegerRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundIntegerRelationsProgram_decodes]
  simp [decodeRewrite, atomToken?, elaborateRewrite?, elaborateRewriteCandidate?,
    rewriteVariableNames, termVariableOccurrences, termsVariableOccurrences,
    elaborateAtom?, elaborateAtoms?, elaborateTerms?, elaborateTerm?, quoteRule?,
    quoteAtom?, quoteAtoms?, quoteTerms?, quoteTerm?, sourceVariableToken,
    distinctNames, collectDistinctNames, nameIndex?, nameIndexFrom?,
    equationSides?, binaryShape, mayStartInteger]

theorem notLess_source :
    (authoredProgram[9]?).bind equationSides? =
      some (binaryShape "ground-integer-not-less" ">=" false) := by
  rw [elaborateProgram?_getElem? authoredProgram_source_exact]
  simp only [compositionRewrites, authoredSources, List.flatMap_cons, List.flatMap_nil]
  rw [source_eq_of_decode groundRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundRelationsProgram_decodes,
    source_eq_of_decode groundIntegerRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundIntegerRelationsProgram_decodes]
  simp [decodeRewrite, atomToken?, elaborateRewrite?, elaborateRewriteCandidate?,
    rewriteVariableNames, termVariableOccurrences, termsVariableOccurrences,
    elaborateAtom?, elaborateAtoms?, elaborateTerms?, elaborateTerm?, quoteRule?,
    quoteAtom?, quoteAtoms?, quoteTerms?, quoteTerm?, sourceVariableToken,
    distinctNames, collectDistinctNames, nameIndex?, nameIndexFrom?,
    equationSides?, binaryShape, mayStartInteger]

theorem gap_source :
    (authoredProgram[10]?).bind equationSides? =
      some (binaryShape "ground-integer-gap" "<" true) := by
  rw [elaborateProgram?_getElem? authoredProgram_source_exact]
  simp only [compositionRewrites, authoredSources, List.flatMap_cons, List.flatMap_nil]
  rw [source_eq_of_decode groundRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundRelationsProgram_decodes,
    source_eq_of_decode groundIntegerRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundIntegerRelationsProgram_decodes]
  simp [decodeRewrite, atomToken?, elaborateRewrite?, elaborateRewriteCandidate?,
    rewriteVariableNames, termVariableOccurrences, termsVariableOccurrences,
    elaborateAtom?, elaborateAtoms?, elaborateTerms?, elaborateTerm?, quoteRule?,
    quoteAtom?, quoteAtoms?, quoteTerms?, quoteTerm?, sourceVariableToken,
    distinctNames, collectDistinctNames, nameIndex?, nameIndexFrom?,
    equationSides?, binaryShape, mayStartInteger]

theorem noGap_source :
    (authoredProgram[11]?).bind equationSides? =
      some (binaryShape "ground-integer-no-gap" ">=" true) := by
  rw [elaborateProgram?_getElem? authoredProgram_source_exact]
  simp only [compositionRewrites, authoredSources, List.flatMap_cons, List.flatMap_nil]
  rw [source_eq_of_decode groundRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundRelationsProgram_decodes,
    source_eq_of_decode groundIntegerRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundIntegerRelationsProgram_decodes]
  simp [decodeRewrite, atomToken?, elaborateRewrite?, elaborateRewriteCandidate?,
    rewriteVariableNames, termVariableOccurrences, termsVariableOccurrences,
    elaborateAtom?, elaborateAtoms?, elaborateTerms?, elaborateTerm?, quoteRule?,
    quoteAtom?, quoteAtoms?, quoteTerms?, quoteTerm?, sourceVariableToken,
    distinctNames, collectDistinctNames, nameIndex?, nameIndexFrom?,
    equationSides?, binaryShape, mayStartInteger]

theorem less_run (left right : Int) :
    runAt? authoredProgram 8 (binaryCall "ground-integer-less" left right) =
      some (if left < right then [binaryQuery "ground-integer-less" left right] else []) :=
  binary_run (notLess := false) less_source left right

theorem notLess_run (left right : Int) :
    runAt? authoredProgram 9 (binaryCall "ground-integer-not-less" left right) =
      some (if right ≤ left then [binaryQuery "ground-integer-not-less" left right] else []) :=
  binary_run (notLess := true) notLess_source left right

theorem gap_run (left right : Int) :
    runAt? authoredProgram 10 (binaryCall "ground-integer-gap" left right) =
      some (if left + 1 < right then [binaryQuery "ground-integer-gap" left right] else []) :=
  binary_run (notLess := false) gap_source left right

theorem noGap_run (left right : Int) :
    runAt? authoredProgram 11 (binaryCall "ground-integer-no-gap" left right) =
      some (if right ≤ left + 1 then [binaryQuery "ground-integer-no-gap" left right] else []) :=
  binary_run (notLess := true) noGap_source left right

/-- Two-sided meaning for the selected actual source occurrence. This
reflects the complete answer list, including the supported empty result. -/
theorem binary_meaning {program : Program} {occurrence : Nat}
    {relation : String} {notLess successor : Bool}
    (shape : (program[occurrence]?).bind equationSides? =
      some (binaryShape relation (if notLess then ">=" else "<") successor))
    (left right : Int) (answers : List GroundTerm) :
    (∃ residual, StepAt program occurrence [] (binaryCall relation left right) residual ∧
      AnswersEval residual answers) ↔
    answers = (if (if notLess then right ≤ (if successor then left + 1 else left)
      else (if successor then left + 1 else left) < right)
      then [binaryQuery relation left right] else []) := by
  rw [← runAt?_iff (binary_safe shape), binary_run shape left right]
  exact ⟨fun equal => (Option.some.inj equal).symm, fun equal => congrArg some equal.symm⟩

theorem less_native_type (left right : Int) :
    HornIntegerProviderNativeType.satisfiesNative
      (HornIntegerProviderNativeType.echoNativeType
        (binaryQuery "ground-integer-less" left right))
      (.request authoredProgram ⟨8, binaryCall "ground-integer-less" left right⟩) :=
  binary_echo_native_type (notLess := false) less_source left right

theorem notLess_native_type (left right : Int) :
    HornIntegerProviderNativeType.satisfiesNative
      (HornIntegerProviderNativeType.echoNativeType
        (binaryQuery "ground-integer-not-less" left right))
      (.request authoredProgram ⟨9, binaryCall "ground-integer-not-less" left right⟩) :=
  binary_echo_native_type (notLess := true) notLess_source left right

theorem gap_native_type (left right : Int) :
    HornIntegerProviderNativeType.satisfiesNative
      (HornIntegerProviderNativeType.echoNativeType
        (binaryQuery "ground-integer-gap" left right))
      (.request authoredProgram ⟨10, binaryCall "ground-integer-gap" left right⟩) :=
  binary_echo_native_type (notLess := false) gap_source left right

theorem noGap_native_type (left right : Int) :
    HornIntegerProviderNativeType.satisfiesNative
      (HornIntegerProviderNativeType.echoNativeType
        (binaryQuery "ground-integer-no-gap" left right))
      (.request authoredProgram ⟨11, binaryCall "ground-integer-no-gap" left right⟩) :=
  binary_echo_native_type (notLess := true) noGap_source left right

#print axioms less_native_type
#print axioms gap_native_type

#print axioms less_run
#print axioms notLess_run
#print axioms gap_run
#print axioms noGap_run

private def scalarRelation (negative : Bool) : String :=
  if negative then "ground-non-unicode-scalar" else "ground-unicode-scalar"

def scalarShape (negative : Bool) : Term × Term :=
  let query := Term.app (scalarRelation negative) (.cons (.var 0) .nil)
  let answer := Term.app "quote" (.cons query .nil)
  let empty := Term.app "metta-nullary" (.cons (.atom "empty") .nil)
  let good := if negative then empty else answer
  let bad := if negative then answer else empty
  let branch := fun left right yes no => Term.app "if"
    (.cons (.app "<" (.cons left (.cons right .nil))) (.cons yes (.cons no .nil)))
  (.app ("gslt:" ++ scalarRelation negative) (.cons query .nil),
    branch (.var 0) (.integer 0) bad
      (branch (.integer 1114111) (.var 0) bad
        (branch (.var 0) (.integer 55296) good
          (branch (.integer 57343) (.var 0) good bad))))

theorem unicode_source :
    (authoredProgram[12]?).bind equationSides? = some (scalarShape false) := by
  rw [elaborateProgram?_getElem? authoredProgram_source_exact]
  simp only [compositionRewrites, authoredSources, List.flatMap_cons, List.flatMap_nil]
  rw [source_eq_of_decode groundRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundRelationsProgram_decodes,
    source_eq_of_decode groundIntegerRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundIntegerRelationsProgram_decodes]
  simp [decodeRewrite, atomToken?, elaborateRewrite?, elaborateRewriteCandidate?,
    rewriteVariableNames, termVariableOccurrences, termsVariableOccurrences,
    elaborateAtom?, elaborateAtoms?, elaborateTerms?, elaborateTerm?, quoteRule?,
    quoteAtom?, quoteAtoms?, quoteTerms?, quoteTerm?, sourceVariableToken,
    distinctNames, collectDistinctNames, nameIndex?, nameIndexFrom?,
    equationSides?, scalarShape, scalarRelation, mayStartInteger]

theorem nonUnicode_source :
    (authoredProgram[13]?).bind equationSides? = some (scalarShape true) := by
  rw [elaborateProgram?_getElem? authoredProgram_source_exact]
  simp only [compositionRewrites, authoredSources, List.flatMap_cons, List.flatMap_nil]
  rw [source_eq_of_decode groundRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundRelationsProgram_decodes,
    source_eq_of_decode groundIntegerRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundIntegerRelationsProgram_decodes]
  simp [decodeRewrite, atomToken?, elaborateRewrite?, elaborateRewriteCandidate?,
    rewriteVariableNames, termVariableOccurrences, termsVariableOccurrences,
    elaborateAtom?, elaborateAtoms?, elaborateTerms?, elaborateTerm?, quoteRule?,
    quoteAtom?, quoteAtoms?, quoteTerms?, quoteTerm?, sourceVariableToken,
    distinctNames, collectDistinctNames, nameIndex?, nameIndexFrom?,
    equationSides?, scalarShape, scalarRelation, mayStartInteger]

def scalarQuery (negative : Bool) (scalar : Int) : GroundTerm :=
  .app (scalarRelation negative) (.cons (.integer scalar) .nil)

def scalarCall (negative : Bool) (scalar : Int) : GroundTerm :=
  .app ("gslt:" ++ scalarRelation negative) (.cons (scalarQuery negative scalar) .nil)

private def scalarResidual (negative : Bool) (scalar : Int) : GroundTerm :=
  let answer := GroundTerm.app "quote" (.cons (scalarQuery negative scalar) .nil)
  let empty := GroundTerm.app "metta-nullary" (.cons (.atom "empty") .nil)
  let good := if negative then empty else answer
  let bad := if negative then answer else empty
  let branch := fun left right yes no => GroundTerm.app "if"
    (.cons (.app "<" (.cons (.integer left) (.cons (.integer right) .nil)))
      (.cons yes (.cons no .nil)))
  branch scalar 0 bad (branch 1114111 scalar bad
    (branch scalar 55296 good (branch 57343 scalar good bad)))

private theorem scalar_safe {program : Program} {occurrence : Nat} {negative : Bool}
    (shape : (program[occurrence]?).bind equationSides? = some (scalarShape negative)) :
    OccurrenceRangeSafe program occurrence := by
  apply occurrenceRangeSafe_of_shape shape
  intro identifier occurs
  cases negative <;>
    simp_all [scalarShape, HornSpecialization.termVariables, HornSpecialization.termsVariables]

private theorem scalar_step {program : Program} {occurrence : Nat} {negative : Bool}
    (shape : (program[occurrence]?).bind equationSides? = some (scalarShape negative))
    (scalar : Int) :
    StepAt program occurrence [] (scalarCall negative scalar) (scalarResidual negative scalar) := by
  cases selected : program[occurrence]? with
  | none => simp [selected] at shape
  | some rule =>
    apply StepAt.root
      (left := (scalarShape negative).1) (right := (scalarShape negative).2)
      (substitution := [(0, .integer scalar)]) selected
      (by simpa [selected] using shape)
    · rfl
    · cases negative <;> rfl
    · cases negative <;> rfl

open Mettapedia.GSLT.Parsing.PlainBnfLexicalScalarSemantics

/-- The independently defined scalar predicate, not a copied branch tree,
determines the result of the checked authored source occurrence. -/
theorem scalar_run {program : Program} {occurrence : Nat} {negative : Bool}
    (shape : (program[occurrence]?).bind equationSides? = some (scalarShape negative))
    (scalar : Int) :
    runAt? program occurrence (scalarCall negative scalar) =
      some (if (if negative then !isUnicodeScalarInt scalar else isUnicodeScalarInt scalar)
        then [scalarQuery negative scalar] else []) := by
  apply runAt?_of_step (scalar_safe shape) (scalar_step shape scalar)
  cases negative <;>
    simp only [scalarResidual, Bool.false_eq_true, ite_false, ite_true,
      evalAnswers?, evalBoolean?, evalInteger?]
  all_goals
    by_cases below : scalar < 0
    · simp [below, isUnicodeScalarInt, show ¬ 0 ≤ scalar by omega]
    · by_cases above : 1114111 < scalar
      · simp [below, above, isUnicodeScalarInt, show ¬ scalar ≤ 1114111 by omega]
      · by_cases before : scalar < 55296
        · simp [below, above, before, isUnicodeScalarInt,
            show 0 ≤ scalar by omega, show scalar ≤ 1114111 by omega]
        · by_cases after : 57343 < scalar
          · simp [below, above, before, after, isUnicodeScalarInt,
              show 0 ≤ scalar by omega, show scalar ≤ 1114111 by omega]
          · simp [below, above, before, after, isUnicodeScalarInt]

theorem unicode_run (scalar : Int) :
    runAt? authoredProgram 12 (scalarCall false scalar) =
      some (if isUnicodeScalarInt scalar then [scalarQuery false scalar] else []) :=
  scalar_run unicode_source scalar

theorem nonUnicode_run (scalar : Int) :
    runAt? authoredProgram 13 (scalarCall true scalar) =
      some (if !isUnicodeScalarInt scalar then [scalarQuery true scalar] else []) :=
  scalar_run nonUnicode_source scalar

theorem scalar_meaning {program : Program} {occurrence : Nat} {negative : Bool}
    (shape : (program[occurrence]?).bind equationSides? = some (scalarShape negative))
    (scalar : Int) (answers : List GroundTerm) :
    (∃ residual, StepAt program occurrence [] (scalarCall negative scalar) residual ∧
      AnswersEval residual answers) ↔
    answers = (if (if negative then !isUnicodeScalarInt scalar else isUnicodeScalarInt scalar)
      then [scalarQuery negative scalar] else []) := by
  rw [← runAt?_iff (scalar_safe shape), scalar_run shape scalar]
  exact ⟨fun equal => (Option.some.inj equal).symm, fun equal => congrArg some equal.symm⟩

/-- The selected authored Unicode provider produces its answer exactly on
the independently specified scalar carrier. -/
theorem unicode_source_answer_iff (scalar : Int) :
    (∃ residual, StepAt authoredProgram 12 [] (scalarCall false scalar) residual ∧
      AnswersEval residual [scalarQuery false scalar]) ↔
    isUnicodeScalarInt scalar = true := by
  rw [scalar_meaning unicode_source]
  cases isUnicodeScalarInt scalar <;> simp

/-- An integer gap across the surrogate block is not evidence of a missing
Unicode scalar. The authored graph rules must retain their surrogate case. -/
theorem surrogate_gap_is_numeric_only :
    runAt? authoredProgram 10 (binaryCall "ground-integer-gap" 55295 57344) =
      some [binaryQuery "ground-integer-gap" 55295 57344] ∧
    ¬ ∃ scalar : Int, isUnicodeScalarInt scalar = true ∧
      55295 < scalar ∧ scalar < 57344 := by
  constructor
  · rw [gap_run]
    decide
  · rintro ⟨scalar, valid, above, below⟩
    simp only [isUnicodeScalarInt, decide_eq_true_eq] at valid
    omega

theorem equal_integers_have_no_less_answer (value : Int) :
    runAt? authoredProgram 8 (binaryCall "ground-integer-less" value value) = some [] := by
  simp [less_run]

#print axioms binary_meaning
#print axioms scalar_meaning
#print axioms unicode_source_answer_iff
#print axioms surrogate_gap_is_numeric_only

#print axioms unicode_run
#print axioms nonUnicode_run

end IntegerProviders

namespace ProviderExecution

open Mettapedia.GSLT.Parsing
open HornIntegerProvider
open IntegerProviders

/-- This tactic exposes only rows from the already checked file composition.
Expected rule shapes below are assertions about that source, not replacements. -/
local macro "expose_source_row" decoded:term : tactic => `(tactic| (
  rw [elaborateProgram?_getElem? authoredProgram_source_exact]
  simp only [compositionRewrites, authoredSources, List.flatMap_cons, List.flatMap_nil]
  simp only [List.getElem?_append,
    (source_field_lengths groundRelationsProgram_decodes).2.2.2,
    (source_field_lengths cettaPettaGroundRelationsProgram_decodes).2.2.2,
    (source_field_lengths groundIntegerRelationsProgram_decodes).2.2.2,
    (source_field_lengths cettaPettaGroundIntegerRelationsProgram_decodes).2.2.2,
    (source_field_lengths semanticAdmissionProgram_decodes).2.2.2,
    (source_field_lengths graphAnalysisProgram_decodes).2.2.2,
    Nat.reduceLT, Nat.reduceSub, ↓reduceIte]
  rw [source_eq_of_decode $decoded]
  simp [decodeRewrite, atomToken?, elaborateRewrite?, elaborateRewriteCandidate?,
    rewriteVariableNames, termVariableOccurrences, termsVariableOccurrences,
    elaborateAtom?, elaborateAtoms?, elaborateTerms?, elaborateTerm?, quoteRule?,
    quoteAtom?, quoteAtoms?, quoteTerms?, quoteTerm?, sourceVariableToken,
    distinctNames, collectDistinctNames, nameIndex?, nameIndexFrom?, mayStartInteger]))

def differentShape : Term × Term :=
  let query := Term.app "different" (.cons (.var 0) (.cons (.var 1) .nil))
  (.app "gslt:different" (.cons query .nil),
   .app "if" (.cons (.app "=="
     (.cons (.app "quote" (.cons (.var 0) .nil))
       (.cons (.app "quote" (.cons (.var 1) .nil)) .nil)))
     (.cons (.app "metta-nullary" (.cons (.atom "empty") .nil))
       (.cons (.app "quote" (.cons query .nil)) .nil))))

theorem different_source :
    (authoredProgram[1]?).bind equationSides? = some differentShape := by
  unfold differentShape
  expose_source_row cettaPettaGroundRelationsProgram_decodes
  rfl

theorem different_safe : OccurrenceRangeSafe authoredProgram 1 := by
  apply occurrenceRangeSafe_of_shape different_source
  intro identifier occurs
  simp_all [HornSpecialization.termVariables,
    HornSpecialization.termsVariables]
  omega

def differentGoal (left right : GroundTerm) : GroundAtom :=
  ⟨"different", .cons left (.cons right .nil)⟩

private def differentResidual (left right : GroundTerm) : GroundTerm :=
  .app "if" (.cons (.app "=="
    (.cons (.app "quote" (.cons left .nil))
      (.cons (.app "quote" (.cons right .nil)) .nil)))
    (.cons (.app "metta-nullary" (.cons (.atom "empty") .nil))
      (.cons (.app "quote" (.cons
        (HornProviderGSLT.queryTerm (differentGoal left right)) .nil)) .nil)))

private theorem different_step (left right : GroundTerm) :
    StepAt authoredProgram 1 [] (HornProviderGSLT.providerCall (differentGoal left right))
      (differentResidual left right) := by
  have shape := different_source
  cases selected : authoredProgram[1]? with
  | none => simp [selected] at shape
  | some rule =>
    exact StepAt.root (left := differentShape.1) (right := differentShape.2)
      (substitution := [(0, left), (1, right)]) selected
      (by simpa [selected] using shape) rfl rfl rfl

/-- Exact answers of the actual disequality equation on the alias-free
source fragment. This is not a claim about a complete host term codec. -/
theorem different_run {left right : GroundTerm}
    (leftCanonical : AliasFree left) (rightCanonical : AliasFree right) :
    runAt? authoredProgram 1 (HornProviderGSLT.providerCall (differentGoal left right)) =
      some (if left = right then [] else
        [HornProviderGSLT.queryTerm (differentGoal left right)]) := by
  apply runAt?_of_step different_safe (different_step left right)
  simp [differentResidual, evalAnswers?, evalBoolean?_quotedEqual leftCanonical rightCanonical]
  split_ifs <;> rfl

theorem different_meaning {left right : GroundTerm}
    (leftCanonical : AliasFree left) (rightCanonical : AliasFree right)
    (answers : List GroundTerm) :
    (∃ residual, StepAt authoredProgram 1 []
      (HornProviderGSLT.providerCall (differentGoal left right)) residual ∧
      AnswersEval residual answers) ↔
    answers = (if left = right then [] else
      [HornProviderGSLT.queryTerm (differentGoal left right)]) := by
  rw [← runAt?_iff different_safe, different_run leftCanonical rightCanonical]
  exact ⟨fun equal => (Option.some.inj equal).symm,
    fun equal => congrArg some equal.symm⟩

/-- The existing scalar-list text representation is inside the qualified
fragment even when its characters spell a host Boolean alias. -/
theorem text_aliasFree (text : List Char) :
    AliasFree (PlainBnfSourceTextAppend.encodeText text) := by
  induction text with
  | nil => exact .app (by decide) .nil
  | cons character rest ih =>
    exact .app (by decide) (.cons (.integer _) (.cons ih .nil))

theorem different_text_meaning (left right : List Char) (answers : List GroundTerm) :
    (∃ residual, StepAt authoredProgram 1 []
      (HornProviderGSLT.providerCall (differentGoal
        (PlainBnfSourceTextAppend.encodeText left)
        (PlainBnfSourceTextAppend.encodeText right))) residual ∧
      AnswersEval residual answers) ↔
    answers = (if PlainBnfSourceTextAppend.encodeText left =
        PlainBnfSourceTextAppend.encodeText right then [] else
      [HornProviderGSLT.queryTerm (differentGoal
        (PlainBnfSourceTextAppend.encodeText left)
        (PlainBnfSourceTextAppend.encodeText right))]) :=
  different_meaning (text_aliasFree left) (text_aliasFree right) answers

def capabilityRule (name relation : String) (arity : Nat) : Rule :=
  ⟨name, ⟨"oslf-external-relation-decl-v1",
    .cons (.atom relation) (.cons (.integer arity) .nil)⟩, []⟩

theorem different_capability_source : authoredProgram[0]? =
    some (capabilityRule "ground-different-capability-v1" "different" 2) := by
  unfold capabilityRule
  expose_source_row groundRelationsProgram_decodes

theorem less_capability_source : authoredProgram[2]? =
    some (capabilityRule "ground-integer-less-capability-v1" "ground-integer-less" 2) := by
  unfold capabilityRule
  expose_source_row groundIntegerRelationsProgram_decodes

theorem notLess_capability_source : authoredProgram[3]? =
    some (capabilityRule "ground-integer-not-less-capability-v1" "ground-integer-not-less" 2) := by
  unfold capabilityRule
  expose_source_row groundIntegerRelationsProgram_decodes

def integerGoal (relation : String) (left right : Int) : GroundAtom :=
  ⟨relation, .cons (.integer left) (.cons (.integer right) .nil)⟩

theorem different_query_replay {left right : GroundTerm}
    (leftCanonical : AliasFree left) (rightCanonical : AliasFree right)
    (fuel : Nat) (rest : State) :
    HornProviderGSLT.replayStep? authoredProgram (.provider 0 1)
      ((fuel + 1, differentGoal left right) :: rest) =
      if left = right then none else some rest := by
  have capability : HornProviderGSLT.checkCapability
      (capabilityRule "ground-different-capability-v1" "different" 2)
      (differentGoal left right) = true := by rfl
  simp [HornProviderGSLT.replayStep?, different_capability_source,
    capability, different_run leftCanonical rightCanonical]

theorem less_query_replay (left right : Int) (fuel : Nat) (rest : State) :
    HornProviderGSLT.replayStep? authoredProgram (.provider 2 8)
      ((fuel + 1, integerGoal "ground-integer-less" left right) :: rest) =
      if left < right then some rest else none := by
  have capability : HornProviderGSLT.checkCapability
      (capabilityRule "ground-integer-less-capability-v1" "ground-integer-less" 2)
      (integerGoal "ground-integer-less" left right) = true := by rfl
  have returned : runAt? authoredProgram 8
      (HornProviderGSLT.providerCall (integerGoal "ground-integer-less" left right)) =
      some (if left < right then
        [HornProviderGSLT.queryTerm (integerGoal "ground-integer-less" left right)] else []) :=
    less_run left right
  simp [HornProviderGSLT.replayStep?, less_capability_source, capability, returned]

theorem notLess_query_replay (left right : Int) (fuel : Nat) (rest : State) :
    HornProviderGSLT.replayStep? authoredProgram (.provider 3 9)
      ((fuel + 1, integerGoal "ground-integer-not-less" left right) :: rest) =
      if right ≤ left then some rest else none := by
  have capability : HornProviderGSLT.checkCapability
      (capabilityRule "ground-integer-not-less-capability-v1" "ground-integer-not-less" 2)
      (integerGoal "ground-integer-not-less" left right) = true := by rfl
  have returned : runAt? authoredProgram 9
      (HornProviderGSLT.providerCall (integerGoal "ground-integer-not-less" left right)) =
      some (if right ≤ left then
        [HornProviderGSLT.queryTerm (integerGoal "ground-integer-not-less" left right)] else []) :=
    notLess_run left right
  simp [HornProviderGSLT.replayStep?, notLess_capability_source, capability, returned]

def tailEndRule (negative : Bool) : Rule :=
  ⟨if negative then "bnf-exclusion-tail-complete-v1" else "bnf-exclusion-tail-missing-last-v1",
    ⟨"BNFExclusionTailInhabitedV1", .cons (.var 0)
      (.cons (.app "metta-nullary" (.cons (.atom "bnf-v1:scalars-nil") .nil))
        (.cons (.atom (if negative then "BNFNoV1" else "BNFYesV1")) .nil))⟩,
    [⟨if negative then "ground-integer-not-less" else "ground-integer-less",
      .cons (.var 0) (.cons (.integer 1114111) .nil)⟩]⟩

theorem tailEnd_yes_source : authoredProgram[126]? = some (tailEndRule false) := by
  unfold tailEndRule
  expose_source_row graphAnalysisProgram_decodes

theorem tailEnd_no_source : authoredProgram[127]? = some (tailEndRule true) := by
  unfold tailEndRule
  expose_source_row graphAnalysisProgram_decodes

def tailEndGoal (last : Int) (negative : Bool) : GroundAtom :=
  ⟨"BNFExclusionTailInhabitedV1", .cons (.integer last)
    (.cons (.app "metta-nullary" (.cons (.atom "bnf-v1:scalars-nil") .nil))
      (.cons (.atom (if negative then "BNFNoV1" else "BNFYesV1")) .nil))⟩

/-- The supplied trace follows the authored rule and then its actual provider.
It retains both source occurrences; it is not a first-match search procedure. -/
def tailEndTrace (last : Int) (negative : Bool) : List HornProviderGSLT.Action :=
  [.rule (if negative then 127 else 126) [(0, .integer last)],
   .provider (if negative then 3 else 2) (if negative then 9 else 8)]

theorem tailEnd_first_replay (last : Int) (negative : Bool) (fuel : Nat) (rest : State) :
    HornProviderGSLT.replayStep? authoredProgram
      (.rule (if negative then 127 else 126) [(0, .integer last)])
      ((fuel + 1, tailEndGoal last negative) :: rest) =
      some ((fuel, integerGoal
        (if negative then "ground-integer-not-less" else "ground-integer-less")
        last 1114111) :: rest) := by
  apply HornProviderGSLT.replayStep?_complete (by trivial)
  cases negative
  · exact HornProviderGSLT.Step.rule
      (goals := [integerGoal "ground-integer-less" last 1114111])
      tailEnd_yes_source rfl rfl rfl
  · exact HornProviderGSLT.Step.rule
      (goals := [integerGoal "ground-integer-not-less" last 1114111])
      tailEnd_no_source rfl rfl rfl

theorem tailEnd_replay (last : Int) (negative : Bool) (fuel : Nat) (rest : State) :
    HornProviderGSLT.replayPath? authoredProgram (tailEndTrace last negative)
      ((fuel + 2, tailEndGoal last negative) :: rest) =
      if (if negative then 1114111 ≤ last else last < 1114111) then some rest else none := by
  cases negative
  · simp only [tailEndTrace, Bool.false_eq_true, ↓reduceIte, HornProviderGSLT.replayPath?]
    have first := tailEnd_first_replay last false (fuel + 1) rest
    simp only [Bool.false_eq_true, ↓reduceIte, Nat.add_assoc, Nat.reduceAdd] at first
    rw [first]
    simp [less_query_replay]
  · simp only [tailEndTrace, ↓reduceIte, HornProviderGSLT.replayPath?]
    have first := tailEnd_first_replay last true (fuel + 1) rest
    simp only [↓reduceIte, Nat.add_assoc, Nat.reduceAdd] at first
    rw [first]
    simp [notLess_query_replay]

theorem tailEnd_trace_safe (last : Int) (negative : Bool) :
    HornProviderGSLT.TraceSafe authoredProgram (tailEndTrace last negative) := by
  cases negative
  · simp [HornProviderGSLT.TraceSafe, tailEndTrace, HornProviderGSLT.ActionSafe,
      binary_safe less_source]
  · simp [HornProviderGSLT.TraceSafe, tailEndTrace, HornProviderGSLT.ActionSafe,
      binary_safe notLess_source]

/-- Reflection concerns this retained two-action trace, not arbitrary search.
The untouched suffix may itself contain duplicates and unfinished obligations. -/
theorem tailEnd_path_iff (last : Int) (negative : Bool) (fuel : Nat) (rest : State) :
    HornProviderGSLT.Path authoredProgram (tailEndTrace last negative)
      ((fuel + 2, tailEndGoal last negative) :: rest) rest ↔
      (if negative then 1114111 ≤ last else last < 1114111) := by
  rw [← HornProviderGSLT.replayPath?_iff (tailEnd_trace_safe last negative), tailEnd_replay]
  split_ifs <;> simp_all

/-- The source endpoint branch agrees with the independently defined scan.
This is its empty-tail case, not the full recursive exclusion theorem. -/
theorem tailEnd_scan_agreement (last : Nat) (negative : Bool) (fuel : Nat) (rest : State) :
    HornProviderGSLT.Path authoredProgram (tailEndTrace last negative)
      ((fuel + 2, tailEndGoal last negative) :: rest) rest ↔
      negative = !(PlainBnfLexicalInhabitation.gapAfter last []) := by
  rw [tailEnd_path_iff]
  cases negative <;> simp [PlainBnfLexicalInhabitation.gapAfter]

theorem gap_capability_source : authoredProgram[4]? =
    some (capabilityRule "ground-integer-gap-capability-v1" "ground-integer-gap" 2) := by
  unfold capabilityRule
  expose_source_row groundIntegerRelationsProgram_decodes

theorem gap_query_replay (left right : Int) (fuel : Nat) (rest : State) :
    HornProviderGSLT.replayStep? authoredProgram (.provider 4 10)
      ((fuel + 1, integerGoal "ground-integer-gap" left right) :: rest) =
      if left + 1 < right then some rest else none := by
  have capability : HornProviderGSLT.checkCapability
      (capabilityRule "ground-integer-gap-capability-v1" "ground-integer-gap" 2)
      (integerGoal "ground-integer-gap" left right) = true := by rfl
  have returned : runAt? authoredProgram 10
      (HornProviderGSLT.providerCall (integerGoal "ground-integer-gap" left right)) =
      some (if left + 1 < right then
        [HornProviderGSLT.queryTerm (integerGoal "ground-integer-gap" left right)] else []) :=
    gap_run left right
  simp [HornProviderGSLT.replayStep?, gap_capability_source, capability, returned]

def scalarPair (left right : Int) : GroundTerm :=
  .app "BNFScalarPairV1" (.cons (.integer left) (.cons (.integer right) .nil))

theorem scalarPair_aliasFree (left right : Int) : AliasFree (scalarPair left right) :=
  (checkAliasFree_iff _).mp rfl

def gapRule : Rule :=
  let pair := Term.app "BNFScalarPairV1" (.cons (.var 0) (.cons (.var 1) .nil))
  let hole := Term.app "BNFScalarPairV1" (.cons (.integer 55295) (.cons (.integer 57344) .nil))
  ⟨"bnf-exclusion-gap-v1",
    ⟨"BNFExclusionGapInhabitedV1", .cons (.var 0) (.cons (.var 1)
      (.cons (.var 2) (.cons (.atom "BNFYesV1") .nil)))⟩,
    [⟨"different", .cons pair (.cons hole .nil)⟩,
     ⟨"ground-integer-gap", .cons (.var 0) (.cons (.var 1) .nil)⟩]⟩

theorem graph_gap_source : authoredProgram[130]? = some gapRule := by
  unfold gapRule
  expose_source_row graphAnalysisProgram_decodes

def gapGoal (left right : Int) (tail : GroundTerm) : GroundAtom :=
  ⟨"BNFExclusionGapInhabitedV1", .cons (.integer left) (.cons (.integer right)
    (.cons tail (.cons (.atom "BNFYesV1") .nil)))⟩

def gapTrace (left right : Int) (tail : GroundTerm) : List HornProviderGSLT.Action :=
  [.rule 130 [(0, .integer left), (1, .integer right), (2, tail)],
   .provider 0 1, .provider 4 10]

theorem gap_first_replay (left right : Int) (tail : GroundTerm)
    (fuel : Nat) (rest : State) :
    HornProviderGSLT.replayStep? authoredProgram
      (.rule 130 [(0, .integer left), (1, .integer right), (2, tail)])
      ((fuel + 1, gapGoal left right tail) :: rest) =
      some ((fuel, differentGoal (scalarPair left right) (scalarPair 55295 57344)) ::
        (fuel, integerGoal "ground-integer-gap" left right) :: rest) := by
  apply HornProviderGSLT.replayStep?_complete (by trivial)
  exact HornProviderGSLT.Step.rule
    (goals := [differentGoal (scalarPair left right) (scalarPair 55295 57344),
      integerGoal "ground-integer-gap" left right]) graph_gap_source rfl rfl rfl

/-- Both authored premises execute in their original order. In particular,
an integer gap across the surrogate interval cannot bypass disequality. -/
theorem gap_replay (left right : Int) (tail : GroundTerm) (fuel : Nat) (rest : State) :
    HornProviderGSLT.replayPath? authoredProgram (gapTrace left right tail)
      ((fuel + 2, gapGoal left right tail) :: rest) =
      if ¬ (left = 55295 ∧ right = 57344) ∧ left + 1 < right then some rest else none := by
  simp only [gapTrace, HornProviderGSLT.replayPath?]
  rw [gap_first_replay left right tail (fuel + 1) rest]
  have different := different_query_replay (scalarPair_aliasFree left right)
    (scalarPair_aliasFree 55295 57344) fuel
    ((fuel + 1, integerGoal "ground-integer-gap" left right) :: rest)
  simp [different]
  simp only [scalarPair, GroundTerm.app.injEq, GroundTerms.cons.injEq,
    GroundTerm.integer.injEq, and_true, true_and]
  split_ifs <;> simp_all [gap_query_replay left right fuel rest]

theorem gap_trace_safe (left right : Int) (tail : GroundTerm) :
    HornProviderGSLT.TraceSafe authoredProgram (gapTrace left right tail) := by
  simp [HornProviderGSLT.TraceSafe, gapTrace, HornProviderGSLT.ActionSafe,
    different_safe, binary_safe gap_source]

theorem gap_path_iff (left right : Int) (tail : GroundTerm) (fuel : Nat) (rest : State) :
    HornProviderGSLT.Path authoredProgram (gapTrace left right tail)
      ((fuel + 2, gapGoal left right tail) :: rest) rest ↔
      ¬ (left = 55295 ∧ right = 57344) ∧ left + 1 < right := by
  rw [← HornProviderGSLT.replayPath?_iff (gap_trace_safe left right tail), gap_replay]
  split_ifs <;> simp_all

/-- This negative uses the real three-action graph trace, not a numeric
provider in isolation. -/
theorem surrogate_gap_trace_refused (tail : GroundTerm) (fuel : Nat) (rest : State) :
    HornProviderGSLT.replayPath? authoredProgram (gapTrace 55295 57344 tail)
      ((fuel + 2, gapGoal 55295 57344 tail) :: rest) = none := by
  simp [gap_replay]

theorem ordinary_gap_trace_accepted (tail : GroundTerm) (fuel : Nat) (rest : State) :
    HornProviderGSLT.replayPath? authoredProgram (gapTrace 0 2 tail)
      ((fuel + 2, gapGoal 0 2 tail) :: rest) = some rest := by
  simp [gap_replay]

theorem adjacent_scalar_trace_refused (tail : GroundTerm) (fuel : Nat) (rest : State) :
    HornProviderGSLT.replayPath? authoredProgram (gapTrace 0 1 tail)
      ((fuel + 2, gapGoal 0 1 tail) :: rest) = none := by
  simp [gap_replay]

namespace RecursiveExclusion

open PlainBnfSourceScalarCodec (encodeScalars)
open PlainBnfLexicalInhabitation (gapAfter exceptGapCheck)

def resultTerm (result : Bool) : GroundTerm :=
  .atom (if result then "BNFYesV1" else "BNFNoV1")

def tailGoal (previous : Nat) (excluded : List Nat) (result : Bool) : GroundAtom :=
  ⟨"BNFExclusionTailInhabitedV1", .cons (.integer previous)
    (.cons (encodeScalars excluded) (.cons (resultTerm result) .nil))⟩

def gapGoal (previous next : Nat) (excluded : List Nat) (result : Bool) : GroundAtom :=
  ⟨"BNFExclusionGapInhabitedV1", .cons (.integer previous) (.cons (.integer next)
    (.cons (encodeScalars excluded) (.cons (resultTerm result) .nil)))⟩

def tailConsRule : Rule :=
  ⟨"bnf-exclusion-tail-cons-v1",
    ⟨"BNFExclusionTailInhabitedV1", .cons (.var 0)
      (.cons (.app "bnf-v1:scalars-cons" (.cons (.var 1) (.cons (.var 2) .nil)))
        (.cons (.var 3) .nil))⟩,
    [⟨"BNFExclusionGapInhabitedV1", .cons (.var 0) (.cons (.var 1)
      (.cons (.var 2) (.cons (.var 3) .nil)))⟩]⟩

def surrogateRule : Rule :=
  ⟨"bnf-exclusion-surrogate-interval-v1",
    ⟨"BNFExclusionGapInhabitedV1", .cons (.integer 55295) (.cons (.integer 57344)
      (.cons (.var 0) (.cons (.var 1) .nil)))⟩,
    [⟨"BNFExclusionTailInhabitedV1", .cons (.integer 57344)
      (.cons (.var 0) (.cons (.var 1) .nil))⟩]⟩

def noGapRule : Rule :=
  let pair := Term.app "BNFScalarPairV1" (.cons (.var 0) (.cons (.var 1) .nil))
  let hole := Term.app "BNFScalarPairV1" (.cons (.integer 55295) (.cons (.integer 57344) .nil))
  ⟨"bnf-exclusion-no-gap-v1",
    ⟨"BNFExclusionGapInhabitedV1", .cons (.var 0) (.cons (.var 1)
      (.cons (.var 2) (.cons (.var 3) .nil)))⟩,
    [⟨"different", .cons pair (.cons hole .nil)⟩,
     ⟨"ground-integer-no-gap", .cons (.var 0) (.cons (.var 1) .nil)⟩,
     ⟨"BNFExclusionTailInhabitedV1", .cons (.var 1)
       (.cons (.var 2) (.cons (.var 3) .nil))⟩]⟩

theorem tailCons_source : authoredProgram[128]? = some tailConsRule := by
  unfold tailConsRule
  expose_source_row graphAnalysisProgram_decodes

theorem surrogate_source : authoredProgram[129]? = some surrogateRule := by
  unfold surrogateRule
  expose_source_row graphAnalysisProgram_decodes

theorem noGap_source : authoredProgram[131]? = some noGapRule := by
  unfold noGapRule
  expose_source_row graphAnalysisProgram_decodes

theorem noGap_capability_source : authoredProgram[5]? =
    some (capabilityRule "ground-integer-no-gap-capability-v1" "ground-integer-no-gap" 2) := by
  unfold capabilityRule
  expose_source_row groundIntegerRelationsProgram_decodes

theorem noGap_query_replay (left right : Int) (fuel : Nat) (rest : State) :
    HornProviderGSLT.replayStep? authoredProgram (.provider 5 11)
      ((fuel + 1, integerGoal "ground-integer-no-gap" left right) :: rest) =
      if right ≤ left + 1 then some rest else none := by
  have capability : HornProviderGSLT.checkCapability
      (capabilityRule "ground-integer-no-gap-capability-v1" "ground-integer-no-gap" 2)
      (integerGoal "ground-integer-no-gap" left right) = true := by rfl
  have returned : runAt? authoredProgram 11
      (HornProviderGSLT.providerCall (integerGoal "ground-integer-no-gap" left right)) =
      some (if right ≤ left + 1 then
        [HornProviderGSLT.queryTerm (integerGoal "ground-integer-no-gap" left right)] else []) :=
    IntegerProviders.noGap_run left right
  simp [HornProviderGSLT.replayStep?, noGap_capability_source, capability, returned]

def tailConsAction (previous next : Nat) (excluded : List Nat) (result : Bool) :
    HornProviderGSLT.Action :=
  .rule 128 [(0, .integer previous), (1, .integer next),
    (2, encodeScalars excluded), (3, resultTerm result)]

theorem tailCons_step (previous next : Nat) (excluded : List Nat)
    (result : Bool) (fuel : Nat) (rest : State) :
    HornProviderGSLT.Step authoredProgram (tailConsAction previous next excluded result)
      ((fuel + 1, tailGoal previous (next :: excluded) result) :: rest)
      ((fuel, gapGoal previous next excluded result) :: rest) := by
  exact HornProviderGSLT.Step.rule
    (goals := [gapGoal previous next excluded result]) tailCons_source rfl rfl rfl

def surrogateAction (excluded : List Nat) (result : Bool) : HornProviderGSLT.Action :=
  .rule 129 [(0, encodeScalars excluded), (1, resultTerm result)]

theorem surrogate_step (excluded : List Nat) (result : Bool) (fuel : Nat) (rest : State) :
    HornProviderGSLT.Step authoredProgram (surrogateAction excluded result)
      ((fuel + 1, gapGoal 55295 57344 excluded result) :: rest)
      ((fuel, tailGoal 57344 excluded result) :: rest) := by
  exact HornProviderGSLT.Step.rule (goals := [tailGoal 57344 excluded result])
    surrogate_source rfl rfl rfl

def noGapActions (previous next : Nat) (excluded : List Nat) (result : Bool) :
    List HornProviderGSLT.Action :=
  [.rule 131 [(0, .integer previous), (1, .integer next),
      (2, encodeScalars excluded), (3, resultTerm result)],
   .provider 0 1, .provider 5 11]

private theorem traceSafe_cons {action : HornProviderGSLT.Action}
    {actions : List HornProviderGSLT.Action}
    (head : HornProviderGSLT.ActionSafe authoredProgram action)
    (tail : HornProviderGSLT.TraceSafe authoredProgram actions) :
    HornProviderGSLT.TraceSafe authoredProgram (action :: actions) := by
  intro candidate member
  rcases List.mem_cons.mp member with rfl | later
  · exact head
  · exact tail candidate later

private theorem traceSafe_append {left right : List HornProviderGSLT.Action}
    (first : HornProviderGSLT.TraceSafe authoredProgram left)
    (second : HornProviderGSLT.TraceSafe authoredProgram right) :
    HornProviderGSLT.TraceSafe authoredProgram (left ++ right) := by
  intro candidate member
  rcases List.mem_append.mp member with earlier | later
  · exact first candidate earlier
  · exact second candidate later

theorem noGap_safe (previous next : Nat) (excluded : List Nat) (result : Bool) :
    HornProviderGSLT.TraceSafe authoredProgram (noGapActions previous next excluded result) := by
  simp [HornProviderGSLT.TraceSafe, noGapActions, HornProviderGSLT.ActionSafe,
    different_safe, binary_safe IntegerProviders.noGap_source]

theorem noGap_prefix (previous next : Nat) (excluded : List Nat)
    (result : Bool) (fuel : Nat) (rest : State)
    (notHole : ¬ (previous = 55295 ∧ next = 57344))
    (adjacent : next ≤ previous + 1) :
    HornProviderGSLT.Path authoredProgram (noGapActions previous next excluded result)
      ((fuel + 2, gapGoal previous next excluded result) :: rest)
      ((fuel + 1, tailGoal next excluded result) :: rest) := by
  have different : scalarPair previous next ≠ scalarPair 55295 57344 := by
    intro same
    have sameInt : (previous : Int) = 55295 ∧ (next : Int) = 57344 := by
      simpa [scalarPair] using same
    apply notHole
    exact_mod_cast sameInt
  have adjacentInt : (next : Int) ≤ (previous : Int) + 1 := by exact_mod_cast adjacent
  let waiting : State := (fuel + 1, tailGoal next excluded result) :: rest
  let numerical : State := (fuel + 1, integerGoal "ground-integer-no-gap" previous next) :: waiting
  have differentStep : HornProviderGSLT.Step authoredProgram (.provider 0 1)
      ((fuel + 1, differentGoal (scalarPair previous next) (scalarPair 55295 57344)) :: numerical)
      numerical := HornProviderGSLT.replayStep?_sound _ _ _ _ (by
        rw [different_query_replay (scalarPair_aliasFree previous next)
          (scalarPair_aliasFree 55295 57344)]
        simp [different])
  have numericalStep : HornProviderGSLT.Step authoredProgram (.provider 5 11)
      numerical waiting := HornProviderGSLT.replayStep?_sound _ _ _ _ (by
        change HornProviderGSLT.replayStep? authoredProgram (.provider 5 11)
          ((fuel + 1, integerGoal "ground-integer-no-gap" previous next) :: waiting) = some waiting
        rw [noGap_query_replay]
        simp [adjacentInt])
  exact HornProviderGSLT.Path.cons (HornProviderGSLT.Step.rule
      (goals := [differentGoal (scalarPair previous next) (scalarPair 55295 57344),
        integerGoal "ground-integer-no-gap" previous next,
        tailGoal next excluded result]) noGap_source rfl rfl rfl)
    (.cons differentStep (.cons numericalStep (.nil waiting)))

/-- This constructs a complete source execution for every finite input list.
The trace is proof evidence, not a replacement native scan implementation. -/
theorem tail_execution (previous : Nat) (excluded : List Nat) (fuel : Nat) (rest : State) :
    ∃ actions, HornProviderGSLT.TraceSafe authoredProgram actions ∧
      HornProviderGSLT.Path authoredProgram actions
      ((fuel + 2 * excluded.length + 2,
        tailGoal previous excluded (gapAfter previous excluded)) :: rest) rest := by
  induction excluded generalizing previous with
  | nil =>
    refine ⟨tailEndTrace previous (!(gapAfter previous [])), tailEnd_trace_safe _ _, ?_⟩
    have path := (tailEnd_scan_agreement previous (!(gapAfter previous [])) fuel rest).mpr rfl
    have same : tailEndGoal previous (!(gapAfter previous [])) =
        tailGoal previous [] (gapAfter previous []) := by
      generalize gapAfter previous [] = result
      cases result <;> rfl
    rw [same] at path
    simpa using path
  | cons next excluded ih =>
    have smaller := ih next
    obtain ⟨actions, safe, recursive⟩ := smaller
    by_cases hole : previous = 55295 ∧ next = 57344
    · rcases hole with ⟨rfl, rfl⟩
      refine ⟨tailConsAction 55295 57344 excluded (gapAfter 57344 excluded) ::
        surrogateAction excluded (gapAfter 57344 excluded) :: actions,
        traceSafe_cons (by trivial) (traceSafe_cons (by trivial) safe), ?_⟩
      have combined := HornProviderGSLT.Path.cons
        (tailCons_step 55295 57344 excluded (gapAfter 57344 excluded)
          (fuel + 2 * excluded.length + 3) rest)
        (HornProviderGSLT.Path.cons
          (surrogate_step excluded (gapAfter 57344 excluded)
            (fuel + 2 * excluded.length + 2) rest) recursive)
      simpa [gapAfter, Nat.mul_add, Nat.add_assoc] using combined
    · by_cases gap : previous + 1 < next
      · have branch := (gap_path_iff previous next (encodeScalars excluded)
          (fuel + 2 * excluded.length + 1) rest).mpr (by
            constructor
            · exact_mod_cast hole
            · exact_mod_cast gap)
        refine ⟨tailConsAction previous next excluded true ::
          gapTrace previous next (encodeScalars excluded),
          traceSafe_cons (by trivial) (gap_trace_safe _ _ _), ?_⟩
        have combined := HornProviderGSLT.Path.cons
          (tailCons_step previous next excluded true (fuel + 2 * excluded.length + 3) rest)
          (by simpa [gapGoal, ProviderExecution.gapGoal, resultTerm, Nat.add_assoc] using branch)
        simpa [gapAfter, hole, gap, Nat.mul_add, Nat.add_assoc] using combined
      · refine ⟨tailConsAction previous next excluded (gapAfter next excluded) ::
          noGapActions previous next excluded (gapAfter next excluded) ++ actions,
          traceSafe_cons (by trivial) (traceSafe_append (noGap_safe _ _ _ _) safe), ?_⟩
        have leading := noGap_prefix previous next excluded (gapAfter next excluded)
          (fuel + 2 * excluded.length + 1) rest hole (by omega)
        have combined := HornProviderGSLT.Path.cons
          (tailCons_step previous next excluded (gapAfter next excluded)
            (fuel + 2 * excluded.length + 3) rest)
          (leading.append recursive)
        simpa [gapAfter, hole, gap, Nat.mul_add, Nat.add_assoc] using combined

theorem tail_replay (previous : Nat) (excluded : List Nat) (fuel : Nat) (rest : State) :
    ∃ actions, HornProviderGSLT.replayPath? authoredProgram actions
      ((fuel + 2 * excluded.length + 2,
        tailGoal previous excluded (gapAfter previous excluded)) :: rest) = some rest := by
  obtain ⟨actions, safe, path⟩ := tail_execution previous excluded fuel rest
  exact ⟨actions, HornProviderGSLT.replayPath?_complete safe path⟩

def exclusionGoal (excluded : List Nat) (result : Bool) : GroundAtom :=
  ⟨"BNFLexicalMatcherInhabitedV1",
    .cons (.app "bnf-v1:lexical-except" (.cons (encodeScalars excluded) .nil))
      (.cons (resultTerm result) .nil)⟩

def emptyExclusionRule : Rule :=
  ⟨"bnf-exclusion-empty-inhabited-v1",
    ⟨"BNFLexicalMatcherInhabitedV1",
      .cons (.app "bnf-v1:lexical-except"
        (.cons (.app "metta-nullary" (.cons (.atom "bnf-v1:scalars-nil") .nil)) .nil))
        (.cons (.atom "BNFYesV1") .nil)⟩, []⟩

def zeroExclusionRule : Rule :=
  ⟨"bnf-exclusion-zero-inhabited-v1",
    ⟨"BNFLexicalMatcherInhabitedV1",
      .cons (.app "bnf-v1:lexical-except"
        (.cons (.app "bnf-v1:scalars-cons" (.cons (.integer 0) (.cons (.var 0) .nil))) .nil))
        (.cons (.var 1) .nil)⟩,
    [⟨"BNFExclusionTailInhabitedV1", .cons (.integer 0)
      (.cons (.var 0) (.cons (.var 1) .nil))⟩]⟩

def missingZeroRule : Rule :=
  ⟨"bnf-exclusion-missing-zero-inhabited-v1",
    ⟨"BNFLexicalMatcherInhabitedV1",
      .cons (.app "bnf-v1:lexical-except"
        (.cons (.app "bnf-v1:scalars-cons" (.cons (.var 0) (.cons (.var 1) .nil))) .nil))
        (.cons (.atom "BNFYesV1") .nil)⟩,
    [⟨"different", .cons (.var 0) (.cons (.integer 0) .nil)⟩]⟩

theorem emptyExclusion_source : authoredProgram[123]? = some emptyExclusionRule := by
  unfold emptyExclusionRule
  expose_source_row graphAnalysisProgram_decodes

theorem zeroExclusion_source : authoredProgram[124]? = some zeroExclusionRule := by
  unfold zeroExclusionRule
  expose_source_row graphAnalysisProgram_decodes

theorem missingZero_source : authoredProgram[125]? = some missingZeroRule := by
  unfold missingZeroRule
  expose_source_row graphAnalysisProgram_decodes

theorem exclusion_execution (excluded : List Nat) (fuel : Nat) (rest : State) :
    ∃ actions, HornProviderGSLT.TraceSafe authoredProgram actions ∧
      HornProviderGSLT.Path authoredProgram actions
        ((fuel + 2 * excluded.length + 2,
          exclusionGoal excluded (exceptGapCheck excluded)) :: rest) rest := by
  cases excluded with
  | nil =>
    refine ⟨[.rule 123 []], by simp [HornProviderGSLT.TraceSafe, HornProviderGSLT.ActionSafe], ?_⟩
    exact .cons (HornProviderGSLT.Step.rule (goals := []) emptyExclusion_source rfl rfl rfl)
      (.nil rest)
  | cons first excluded =>
    by_cases zero : first = 0
    · subst first
      obtain ⟨actions, safe, recursive⟩ := tail_execution 0 excluded (fuel + 1) rest
      let action : HornProviderGSLT.Action :=
        .rule 124 [(0, encodeScalars excluded), (1, resultTerm (gapAfter 0 excluded))]
      refine ⟨action :: actions, traceSafe_cons (by trivial) safe, ?_⟩
      have firstStep : HornProviderGSLT.Step authoredProgram action
          ((fuel + 2 * excluded.length + 4,
            exclusionGoal (0 :: excluded) (gapAfter 0 excluded)) :: rest)
          ((fuel + 2 * excluded.length + 3, tailGoal 0 excluded (gapAfter 0 excluded)) :: rest) :=
        HornProviderGSLT.Step.rule (goals := [tailGoal 0 excluded (gapAfter 0 excluded)])
          zeroExclusion_source rfl rfl rfl
      have tailPath : HornProviderGSLT.Path authoredProgram actions
          ((fuel + 2 * excluded.length + 3, tailGoal 0 excluded (gapAfter 0 excluded)) :: rest) rest := by
        have depth : fuel + 1 + 2 * excluded.length + 2 =
            fuel + 2 * excluded.length + 3 := by omega
        simpa only [depth] using recursive
      simpa [exceptGapCheck, Nat.mul_add, Nat.add_assoc] using
        HornProviderGSLT.Path.cons firstStep tailPath
    · let action : HornProviderGSLT.Action :=
        .rule 125 [(0, .integer first), (1, encodeScalars excluded)]
      refine ⟨[action, .provider 0 1],
        traceSafe_cons (by trivial) (traceSafe_cons different_safe (by
          simp [HornProviderGSLT.TraceSafe])), ?_⟩
      have firstStep : HornProviderGSLT.Step authoredProgram action
          ((fuel + 2 * excluded.length + 4, exclusionGoal (first :: excluded) true) :: rest)
          ((fuel + 2 * excluded.length + 3, differentGoal (.integer first) (.integer 0)) :: rest) :=
        HornProviderGSLT.Step.rule
          (goals := [differentGoal (.integer first) (.integer 0)]) missingZero_source rfl rfl rfl
      have providerStep : HornProviderGSLT.Step authoredProgram (.provider 0 1)
          ((fuel + 2 * excluded.length + 3, differentGoal (.integer first) (.integer 0)) :: rest)
          rest := HornProviderGSLT.replayStep?_sound _ _ _ _ (by
            have checked := different_query_replay (.integer first) (.integer 0)
              (fuel + 2 * excluded.length + 2) rest
            simpa [zero, Nat.add_assoc] using checked)
      simpa [exceptGapCheck, zero, Nat.mul_add, Nat.add_assoc] using
        HornProviderGSLT.Path.cons firstStep (.cons providerStep (.nil rest))

theorem exclusion_replay (excluded : List Nat) (fuel : Nat) (rest : State) :
    ∃ actions, HornProviderGSLT.replayPath? authoredProgram actions
      ((fuel + 2 * excluded.length + 2,
        exclusionGoal excluded (exceptGapCheck excluded)) :: rest) = some rest := by
  obtain ⟨actions, safe, path⟩ := exclusion_execution excluded fuel rest
  exact ⟨actions, HornProviderGSLT.replayPath?_complete safe path⟩

/-- Validity and ordering connect the executed Boolean to actual inhabitation.
This is the forward construction; the arbitrary-path converse is established
below after checking every source clause and provider against the local model. -/
theorem exclusion_inhabitation_realized (excluded : List Nat)
    (valid : ∀ scalar ∈ excluded,
      Mettapedia.GSLT.Parsing.ParserProfileSemantics.isUnicodeScalar scalar = true)
    (sorted : excluded.Pairwise (· < ·)) :
    ∃ actions, HornProviderGSLT.replayPath? authoredProgram actions
        [(2 * excluded.length + 2, exclusionGoal excluded (exceptGapCheck excluded))] = some [] ∧
      (exceptGapCheck excluded = true ↔
        PlainBnfLexicalInhabitation.InhabitedClass (.except excluded)) := by
  obtain ⟨actions, checked⟩ := exclusion_replay excluded 0 []
  exact ⟨actions, by simpa using checked,
    PlainBnfLexicalInhabitation.exceptGapCheck_iff excluded valid sorted⟩

/-- The negative result for the entire scalar carrier follows symbolically
from the finite-input theorem; no million-entry execution is claimed here. -/
theorem full_exclusion_negative_replay :
    ∃ actions, HornProviderGSLT.replayPath? authoredProgram actions
      [(2 * PlainBnfLexicalInhabitation.allScalars.length + 2,
        exclusionGoal PlainBnfLexicalInhabitation.allScalars false)] = some [] := by
  have checked := exclusion_replay PlainBnfLexicalInhabitation.allScalars 0 []
  simpa only [Nat.zero_add,
    PlainBnfLexicalInhabitation.full_exclusion_gap_check_is_false] using checked

/-- A successfully lowered source scalar list is the very input to the
authored scan, and decodes to the same ordered list on the existing wire.
This is a source/wire theorem, not physical C or generated-mode correctness. -/
theorem lowered_exclusion_realized (source : GroundTerm)
    (wire : Mettapedia.GSLT.LanguageDef.CettaWire.Term)
    (lowered : PlainBnfSourceScalarCodec.lowerScalars source = some wire) :
    ∃ excluded actions,
      source = encodeScalars excluded ∧
      PlainBnfStructuredValueCodec.decodeScalars wire = some excluded ∧
      HornProviderGSLT.replayPath? authoredProgram actions
        [(2 * excluded.length + 2,
          ⟨"BNFLexicalMatcherInhabitedV1",
            .cons (.app "bnf-v1:lexical-except" (.cons source .nil))
              (.cons (resultTerm (exceptGapCheck excluded)) .nil)⟩)] = some [] := by
  obtain ⟨excluded, rfl, rfl⟩ :=
    PlainBnfSourceScalarCodec.lowerScalars_reflects source wire lowered
  obtain ⟨actions, checked⟩ := exclusion_replay excluded 0 []
  exact ⟨excluded, actions, rfl, by simp, by simpa [exclusionGoal] using checked⟩

/-- These head names select the entire source family, including both point
and exclusion classes. The inventory below does not select only good traces. -/
def scanRelation (relation : String) : Bool :=
  relation == "BNFLexicalMatcherInhabitedV1" ||
  relation == "BNFExclusionTailInhabitedV1" ||
  relation == "BNFExclusionGapInhabitedV1"

theorem source_scan_family_inventory :
    (compositionRewrites authoredSources).map (fun row =>
      (HornCertificateBoundary.sourceRelation? row.head).any scanRelation) =
      List.replicate 121 false ++ List.replicate 11 true ++ List.replicate 42 false := by
  let mark := fun (row : Rewrite) =>
    (HornCertificateBoundary.sourceRelation? row.head).any scanRelation
  have first : groundRelationsSource.rewrites.map mark = List.replicate 1 false := by
    rw [source_eq_of_decode groundRelationsProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, scanRelation]
  have second : cettaPettaGroundRelationsSource.rewrites.map mark = List.replicate 1 false := by
    rw [source_eq_of_decode cettaPettaGroundRelationsProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, scanRelation]
  have third : groundIntegerRelationsSource.rewrites.map mark = List.replicate 6 false := by
    rw [source_eq_of_decode groundIntegerRelationsProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, scanRelation]
  have fourth : cettaPettaGroundIntegerRelationsSource.rewrites.map mark = List.replicate 6 false := by
    rw [source_eq_of_decode cettaPettaGroundIntegerRelationsProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, scanRelation]
  have fifth : semanticAdmissionSource.rewrites.map mark = List.replicate 67 false := by
    rw [source_eq_of_decode semanticAdmissionProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, scanRelation]
  have sixth : graphAnalysisSource.rewrites.map mark =
      List.replicate 40 false ++ List.replicate 11 true ++ List.replicate 42 false := by
    rw [source_eq_of_decode graphAnalysisProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, scanRelation]
  change (compositionRewrites authoredSources).map mark = _
  simp only [compositionRewrites, authoredSources, List.flatMap_cons, List.flatMap_nil,
    List.map_append, first, second, third, fourth, fifth, sixth, List.append_nil]
  decide +kernel

private theorem headObservation_exact (observe : String → Bool) :
    authoredProgram.map (fun rule => observe rule.head.relation) =
      (compositionRewrites authoredSources).map (fun row =>
        (HornCertificateBoundary.sourceRelation? row.head).any observe) := by
  apply List.ext_getElem?
  intro index
  have same := elaborateProgram?_getElem?_head_relation authoredProgram_source_exact index
  have marked := congrArg (Option.map observe) same
  cases selected : (compositionRewrites authoredSources)[index]? with
  | none => simpa [List.getElem?_map, selected] using marked
  | some row =>
    obtain ⟨compiled, compiledAt⟩ : ∃ rule, authoredProgram[index]? = some rule := by
      have within : index < authoredProgram.length := by
        rw [elaborateProgram?_preserves_length authoredProgram_source_exact]
        by_contra outside
        have absent : (compositionRewrites authoredSources)[index]? = none :=
          List.getElem?_eq_none (by omega)
        rw [absent] at selected
        cases selected
      exact ⟨authoredProgram[index], List.getElem?_eq_getElem within⟩
    obtain ⟨sourceRow, arguments, sourceAt, _, shape⟩ :=
      elaborateProgram?_getElem?_head authoredProgram_source_exact compiledAt
    have rowsEqual : sourceRow = row := Option.some.inj (sourceAt.symm.trans selected)
    subst sourceRow
    simp [List.getElem?_map, selected, compiledAt,
      HornCertificateBoundary.sourceRelation?, shape]

theorem program_scan_family_inventory :
    authoredProgram.map (fun rule => scanRelation rule.head.relation) =
      List.replicate 121 false ++ List.replicate 11 true ++ List.replicate 42 false := by
  rw [headObservation_exact]
  exact source_scan_family_inventory

/-- The two source protocols used by provider discharge. -/
def protocolRelation (relation : String) : Bool :=
  relation == "oslf-external-relation-decl-v1" || relation == "metta-equation"

/-- Scalar arithmetic and disequality are not ordinary clauses in this
composition; they require independently interpreted provider answers. -/
def scanPrimitiveRelation (relation : String) : Bool :=
  ["different", "ground-integer-less", "ground-integer-not-less",
    "ground-integer-gap", "ground-integer-no-gap"].contains relation

theorem program_protocol_family_inventory :
    authoredProgram.map (fun rule => protocolRelation rule.head.relation) =
      List.replicate 14 true ++ List.replicate 160 false := by
  rw [headObservation_exact]
  let mark := fun (row : Rewrite) =>
    (HornCertificateBoundary.sourceRelation? row.head).any protocolRelation
  have part0 : groundRelationsSource.rewrites.map mark = List.replicate 1 true := by
    rw [source_eq_of_decode groundRelationsProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, protocolRelation]
  have part1 : cettaPettaGroundRelationsSource.rewrites.map mark = List.replicate 1 true := by
    rw [source_eq_of_decode cettaPettaGroundRelationsProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, protocolRelation]
  have part2 : groundIntegerRelationsSource.rewrites.map mark = List.replicate 6 true := by
    rw [source_eq_of_decode groundIntegerRelationsProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, protocolRelation]
  have part3 : cettaPettaGroundIntegerRelationsSource.rewrites.map mark = List.replicate 6 true := by
    rw [source_eq_of_decode cettaPettaGroundIntegerRelationsProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, protocolRelation]
  have part4 : semanticAdmissionSource.rewrites.map mark = List.replicate 67 false := by
    rw [source_eq_of_decode semanticAdmissionProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, protocolRelation]
  have part5 : graphAnalysisSource.rewrites.map mark = List.replicate 93 false := by
    rw [source_eq_of_decode graphAnalysisProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, protocolRelation]
  change (compositionRewrites authoredSources).map mark = _
  simp only [compositionRewrites, authoredSources, List.flatMap_cons, List.flatMap_nil,
    List.map_append, part0, part1, part2, part3, part4, part5, List.append_nil]
  decide +kernel

theorem program_scan_primitive_heads_absent :
    authoredProgram.map (fun rule => scanPrimitiveRelation rule.head.relation) =
      List.replicate 174 false := by
  rw [headObservation_exact]
  let mark := fun (row : Rewrite) =>
    (HornCertificateBoundary.sourceRelation? row.head).any scanPrimitiveRelation
  have part0 : groundRelationsSource.rewrites.map mark = List.replicate 1 false := by
    rw [source_eq_of_decode groundRelationsProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, scanPrimitiveRelation]
  have part1 : cettaPettaGroundRelationsSource.rewrites.map mark = List.replicate 1 false := by
    rw [source_eq_of_decode cettaPettaGroundRelationsProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, scanPrimitiveRelation]
  have part2 : groundIntegerRelationsSource.rewrites.map mark = List.replicate 6 false := by
    rw [source_eq_of_decode groundIntegerRelationsProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, scanPrimitiveRelation]
  have part3 : cettaPettaGroundIntegerRelationsSource.rewrites.map mark = List.replicate 6 false := by
    rw [source_eq_of_decode cettaPettaGroundIntegerRelationsProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, scanPrimitiveRelation]
  have part4 : semanticAdmissionSource.rewrites.map mark = List.replicate 67 false := by
    rw [source_eq_of_decode semanticAdmissionProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, scanPrimitiveRelation]
  have part5 : graphAnalysisSource.rewrites.map mark = List.replicate 93 false := by
    rw [source_eq_of_decode graphAnalysisProgram_decodes]
    simp [mark, decodeRewrite, atomToken?, HornCertificateBoundary.sourceRelation?, scanPrimitiveRelation]
  change (compositionRewrites authoredSources).map mark = _
  simp only [compositionRewrites, authoredSources, List.flatMap_cons, List.flatMap_nil,
    List.map_append, part0, part1, part2, part3, part4, part5, List.append_nil]
  decide +kernel

private theorem instantiated_relation {substitution : Substitution}
    {atom : Atom} {goal : GroundAtom}
    (instantiated : instantiateAtom substitution atom = some goal) :
    goal.relation = atom.relation := by
  cases arguments : instantiateTerms substitution atom.arguments with
  | none => simp [instantiateAtom, arguments] at instantiated
  | some values =>
    have same : ({ relation := atom.relation, arguments := values } : GroundAtom) = goal := by
      simpa [instantiateAtom, arguments] using instantiated
    rw [← same]

theorem scan_rule_index {index : Nat} {rule : Rule}
    (selected : authoredProgram[index]? = some rule)
    (belongs : scanRelation rule.head.relation = true) :
    121 ≤ index ∧ index < 132 := by
  have marked : (authoredProgram.map (fun row => scanRelation row.head.relation))[index]? =
      some true := by simp [List.getElem?_map, selected, belongs]
  rw [program_scan_family_inventory] at marked
  simp only [List.getElem?_append, List.length_append, List.length_replicate,
    List.getElem?_replicate] at marked
  split_ifs at marked <;> simp_all

theorem protocol_rule_index {index : Nat} {rule : Rule}
    (selected : authoredProgram[index]? = some rule)
    (belongs : protocolRelation rule.head.relation = true) : index < 14 := by
  have marked : (authoredProgram.map (fun row => protocolRelation row.head.relation))[index]? =
      some true := by simp [List.getElem?_map, selected, belongs]
  rw [program_protocol_family_inventory] at marked
  simp only [List.getElem?_append, List.length_replicate, List.getElem?_replicate] at marked
  split at marked <;> simp_all

theorem scan_primitive_has_no_clause {index : Nat} {rule : Rule}
    (selected : authoredProgram[index]? = some rule) :
    scanPrimitiveRelation rule.head.relation = false := by
  have marked := congrArg (fun rows : List Bool => rows[index]?) program_scan_primitive_heads_absent
  simp only [List.getElem?_map, selected, Option.map_some, List.getElem?_replicate] at marked
  split at marked
  · exact Option.some.inj marked
  · contradiction

theorem unicode_capability_source : authoredProgram[6]? =
    some (capabilityRule "ground-unicode-scalar-capability-v1" "ground-unicode-scalar" 1) := by
  unfold capabilityRule
  expose_source_row groundIntegerRelationsProgram_decodes

theorem nonUnicode_capability_source : authoredProgram[7]? =
    some (capabilityRule "ground-non-unicode-scalar-capability-v1" "ground-non-unicode-scalar" 1) := by
  unfold capabilityRule
  expose_source_row groundIntegerRelationsProgram_decodes

private theorem equation_not_capability {index : Nat} {declaration : Rule}
    {left right : Term} {goal : GroundAtom}
    (shape : (authoredProgram[index]?).bind equationSides? = some (left, right))
    (selected : authoredProgram[index]? = some declaration)
    (capability : HornProviderGSLT.CapabilityDeclaration declaration goal) : False := by
  have decoded : equationSides? declaration = some (left, right) := by
    simpa [selected] using shape
  have equationHead := (equationSides?_spec decoded).1
  have capabilityHead := capability.2.1
  have names := congrArg Atom.relation (equationHead.symm.trans capabilityHead)
  simp at names

private theorem capability_name_of_row {index arity : Nat} {name relation : String}
    {declaration : Rule} {goal : GroundAtom}
    (source : authoredProgram[index]? = some (capabilityRule name relation arity))
    (selected : authoredProgram[index]? = some declaration)
    (capability : HornProviderGSLT.CapabilityDeclaration declaration goal) :
    goal.relation = relation := by
  have same := Option.some.inj (selected.symm.trans source)
  subst declaration
  have head := capability.2.1
  simp only [capabilityRule, Atom.mk.injEq, true_and,
    Terms.cons.injEq, Term.atom.injEq, Term.integer.injEq, and_true] at head
  exact head.1.symm

theorem capability_relation_inventory {index : Nat} {declaration : Rule} {goal : GroundAtom}
    (selected : authoredProgram[index]? = some declaration)
    (capability : HornProviderGSLT.CapabilityDeclaration declaration goal) :
    goal.relation ∈ ["different", "ground-integer-less", "ground-integer-not-less",
      "ground-integer-gap", "ground-integer-no-gap",
      "ground-unicode-scalar", "ground-non-unicode-scalar"] := by
  have name : declaration.head.relation = "oslf-external-relation-decl-v1" :=
    congrArg Atom.relation capability.2.1
  have bounded := protocol_rule_index selected (by simp [protocolRelation, name])
  interval_cases index
  · simp [capability_name_of_row different_capability_source selected capability]
  · exact (equation_not_capability different_source selected capability).elim
  · simp [capability_name_of_row less_capability_source selected capability]
  · simp [capability_name_of_row notLess_capability_source selected capability]
  · simp [capability_name_of_row gap_capability_source selected capability]
  · simp [capability_name_of_row noGap_capability_source selected capability]
  · simp [capability_name_of_row unicode_capability_source selected capability]
  · simp [capability_name_of_row nonUnicode_capability_source selected capability]
  · exact (equation_not_capability less_source selected capability).elim
  · exact (equation_not_capability notLess_source selected capability).elim
  · exact (equation_not_capability IntegerProviders.gap_source selected capability).elim
  · exact (equation_not_capability IntegerProviders.noGap_source selected capability).elim
  · exact (equation_not_capability unicode_source selected capability).elim
  · exact (equation_not_capability nonUnicode_source selected capability).elim

theorem no_scan_capability {index : Nat} {declaration : Rule} {goal : GroundAtom}
    (selected : authoredProgram[index]? = some declaration)
    (scan : scanRelation goal.relation = true) :
    ¬ HornProviderGSLT.CapabilityDeclaration declaration goal := by
  intro capability
  have names := capability_relation_inventory selected capability
  simp only [List.mem_cons, List.not_mem_nil, or_false] at names
  rcases names with name | name | name | name | name | name | name <;>
    simp [scanRelation, name] at scan

private theorem capability_not_equation {index arity : Nat} {name relation : String}
    {goal : GroundAtom} {residual : GroundTerm}
    (source : authoredProgram[index]? = some (capabilityRule name relation arity))
    (step : StepAt authoredProgram index [] (HornProviderGSLT.providerCall goal) residual) :
    False := by
  cases step with
  | root selected decoded _ _ _ =>
    have same := Option.some.inj (selected.symm.trans source)
    subst_vars
    simp [equationSides?, capabilityRule] at decoded

private theorem equation_call_relation {index : Nat} {relation : String}
    {arguments : Terms} {right : Term} {goal : GroundAtom} {residual : GroundTerm}
    (shape : (authoredProgram[index]?).bind equationSides? =
      some (.app ("gslt:" ++ relation) arguments, right))
    (step : StepAt authoredProgram index [] (HornProviderGSLT.providerCall goal) residual) :
    goal.relation = relation := by
  cases step with
  | @root rule left actualRight substitution _ _ selected decoded _ input _ =>
    have same : (left, actualRight) = (.app ("gslt:" ++ relation) arguments, right) := by
      simpa [selected, decoded] using shape
    cases same
    cases values : instantiateTerms substitution arguments with
    | none => simp [instantiateTerm, values] at input
    | some grounded =>
      have heads : "gslt:" ++ relation = "gslt:" ++ goal.relation := by
        have applied : GroundTerm.app ("gslt:" ++ relation) grounded =
            HornProviderGSLT.providerCall goal := by
          simpa [instantiateTerm, values] using input
        exact (GroundTerm.app.inj applied).1
      simpa using heads.symm

/-- All provider equations in the source composition, not just the equation
selected by an intended execution. The named call is forced by its LHS. -/
theorem provider_equation_inventory {index : Nat} {goal : GroundAtom} {residual : GroundTerm}
    (step : StepAt authoredProgram index [] (HornProviderGSLT.providerCall goal) residual) :
    (index = 1 ∧ goal.relation = "different") ∨
    (index = 8 ∧ goal.relation = "ground-integer-less") ∨
    (index = 9 ∧ goal.relation = "ground-integer-not-less") ∨
    (index = 10 ∧ goal.relation = "ground-integer-gap") ∨
    (index = 11 ∧ goal.relation = "ground-integer-no-gap") ∨
    (index = 12 ∧ goal.relation = "ground-unicode-scalar") ∨
    (index = 13 ∧ goal.relation = "ground-non-unicode-scalar") := by
  have bounded : index < 14 := by
    cases step with
    | root selected decoded _ _ _ =>
      have name := congrArg Atom.relation (equationSides?_spec decoded).1
      exact protocol_rule_index selected (by simp [protocolRelation, name])
  interval_cases index
  · exact (capability_not_equation different_capability_source step).elim
  · have name := equation_call_relation (relation := "different") different_source step
    simp [name]
  · exact (capability_not_equation less_capability_source step).elim
  · exact (capability_not_equation notLess_capability_source step).elim
  · exact (capability_not_equation gap_capability_source step).elim
  · exact (capability_not_equation noGap_capability_source step).elim
  · exact (capability_not_equation unicode_capability_source step).elim
  · exact (capability_not_equation nonUnicode_capability_source step).elim
  · have name := equation_call_relation less_source step
    simp [name]
  · have name := equation_call_relation notLess_source step
    simp [name]
  · have name := equation_call_relation IntegerProviders.gap_source step
    simp [name]
  · have name := equation_call_relation IntegerProviders.noGap_source step
    simp [name]
  · have name := equation_call_relation unicode_source step
    simp [name, IntegerProviders.scalarRelation]
  · have name := equation_call_relation nonUnicode_source step
    simp [name, IntegerProviders.scalarRelation]

private abbrev Model := PlainBnfExclusionLocalModel.Meaning

/-- Local semantic validity of an actual ground clause instance. -/
private def LocalRuleSound (rule : Rule) : Prop :=
  ∀ (substitution : Substitution) (goal : GroundAtom) (premises : List GroundAtom),
    instantiateAtom substitution rule.head = some goal →
    instantiateAtoms substitution rule.body = some premises →
    (∀ premise ∈ premises, Model premise) → Model goal

private theorem instantiate_app (substitution : Substitution) (name : String) (arguments : Terms) :
    instantiateTerm substitution (.app name arguments) =
      (instantiateTerms substitution arguments).map (.app name) := by
  cases values : instantiateTerms substitution arguments <;> simp [instantiateTerm, values]

private theorem instantiate_atom (substitution : Substitution) (name : String) :
    instantiateTerm substitution (.atom name) = some (.atom name) := rfl

private theorem instantiate_integer (substitution : Substitution) (value : Int) :
    instantiateTerm substitution (.integer value) = some (.integer value) := rfl

attribute [local simp] instantiate_app instantiate_atom instantiate_integer
  instantiateAtom instantiateAtoms instantiateTerms

def pointsEmptyRule : Rule :=
  ⟨"bnf-points-empty-uninhabited-v1",
    ⟨"BNFLexicalMatcherInhabitedV1",
      .cons (.app "bnf-v1:lexical-points"
        (.cons (.app "metta-nullary" (.cons (.atom "bnf-v1:scalars-nil") .nil)) .nil))
        (.cons (.atom "BNFNoV1") .nil)⟩, []⟩

def pointsConsRule : Rule :=
  ⟨"bnf-points-cons-inhabited-v1",
    ⟨"BNFLexicalMatcherInhabitedV1",
      .cons (.app "bnf-v1:lexical-points"
        (.cons (.app "bnf-v1:scalars-cons" (.cons (.var 0) (.cons (.var 1) .nil))) .nil))
        (.cons (.atom "BNFYesV1") .nil)⟩, []⟩

theorem pointsEmpty_source : authoredProgram[121]? = some pointsEmptyRule := by
  unfold pointsEmptyRule
  expose_source_row graphAnalysisProgram_decodes

theorem pointsCons_source : authoredProgram[122]? = some pointsConsRule := by
  unfold pointsConsRule
  expose_source_row graphAnalysisProgram_decodes

private theorem pointsEmpty_sound : LocalRuleSound pointsEmptyRule := by
  intro substitution goal premises head body holds
  all_goals simp_all [pointsEmptyRule]
  subst goal premises
  exact PlainBnfExclusionLocalModel.points_local _ _

private theorem pointsCons_sound : LocalRuleSound pointsConsRule := by
  intro substitution goal premises head body holds
  cases v0 : instantiateTerm substitution (.var 0) <;>
    cases v1 : instantiateTerm substitution (.var 1)
  all_goals simp_all [pointsConsRule]
  subst goal premises
  exact PlainBnfExclusionLocalModel.points_local _ _

private theorem emptyExclusion_sound : LocalRuleSound emptyExclusionRule := by
  intro substitution goal premises head body holds
  all_goals simp_all [emptyExclusionRule]
  subst goal premises
  exact PlainBnfExclusionLocalModel.empty_exclusion_local

private theorem zeroExclusion_sound : LocalRuleSound zeroExclusionRule := by
  intro substitution goal premises head body holds
  cases v0 : instantiateTerm substitution (.var 0) <;>
    cases v1 : instantiateTerm substitution (.var 1)
  all_goals simp_all [zeroExclusionRule]
  subst goal premises
  apply PlainBnfExclusionLocalModel.zero_exclusion_local
  aesop

private theorem missingZero_sound : LocalRuleSound missingZeroRule := by
  intro substitution goal premises head body holds
  cases v0 : instantiateTerm substitution (.var 0) <;>
    cases v1 : instantiateTerm substitution (.var 1)
  all_goals simp_all [missingZeroRule]
  subst goal premises
  apply PlainBnfExclusionLocalModel.missing_zero_local
  aesop

private theorem tailEndYes_sound : LocalRuleSound (tailEndRule false) := by
  intro substitution goal premises head body holds
  cases v0 : instantiateTerm substitution (.var 0)
  all_goals simp_all [tailEndRule]
  subst goal premises
  apply PlainBnfExclusionLocalModel.tail_less_local
  aesop

private theorem tailEndNo_sound : LocalRuleSound (tailEndRule true) := by
  intro substitution goal premises head body holds
  cases v0 : instantiateTerm substitution (.var 0)
  all_goals simp_all [tailEndRule]
  subst goal premises
  apply PlainBnfExclusionLocalModel.tail_notLess_local
  aesop

private theorem tailCons_sound : LocalRuleSound tailConsRule := by
  intro substitution goal premises head body holds
  cases v0 : instantiateTerm substitution (.var 0) <;>
    cases v1 : instantiateTerm substitution (.var 1) <;>
    cases v2 : instantiateTerm substitution (.var 2) <;>
    cases v3 : instantiateTerm substitution (.var 3)
  all_goals simp_all [tailConsRule]
  subst goal premises
  apply PlainBnfExclusionLocalModel.tail_cons_local
  aesop

private theorem surrogate_sound : LocalRuleSound surrogateRule := by
  intro substitution goal premises head body holds
  cases v0 : instantiateTerm substitution (.var 0) <;>
    cases v1 : instantiateTerm substitution (.var 1)
  all_goals simp_all [surrogateRule]
  subst goal premises
  apply PlainBnfExclusionLocalModel.surrogate_local
  aesop

private theorem gap_sound : LocalRuleSound gapRule := by
  intro substitution goal premises head body holds
  cases v0 : instantiateTerm substitution (.var 0) <;>
    cases v1 : instantiateTerm substitution (.var 1) <;>
    cases v2 : instantiateTerm substitution (.var 2)
  all_goals simp_all [gapRule]
  subst goal premises
  apply PlainBnfExclusionLocalModel.gap_local <;> aesop

private theorem noGap_sound : LocalRuleSound noGapRule := by
  intro substitution goal premises head body holds
  cases v0 : instantiateTerm substitution (.var 0) <;>
    cases v1 : instantiateTerm substitution (.var 1) <;>
    cases v2 : instantiateTerm substitution (.var 2) <;>
    cases v3 : instantiateTerm substitution (.var 3)
  all_goals simp_all [noGapRule]
  subst goal premises
  apply PlainBnfExclusionLocalModel.no_gap_local <;> aesop

theorem clauses_sound : HornProviderGSLT.ClauseSound authoredProgram Model := by
  intro index rule substitution goal premises selected _valid head body holds
  have relation := instantiated_relation head
  by_cases scan : scanRelation rule.head.relation = true
  · obtain ⟨lower, upper⟩ := scan_rule_index selected scan
    interval_cases index
    · have same := Option.some.inj (selected.symm.trans pointsEmpty_source)
      subst rule
      exact pointsEmpty_sound substitution goal premises head body holds
    · have same := Option.some.inj (selected.symm.trans pointsCons_source)
      subst rule
      exact pointsCons_sound substitution goal premises head body holds
    · have same := Option.some.inj (selected.symm.trans emptyExclusion_source)
      subst rule
      exact emptyExclusion_sound substitution goal premises head body holds
    · have same := Option.some.inj (selected.symm.trans zeroExclusion_source)
      subst rule
      exact zeroExclusion_sound substitution goal premises head body holds
    · have same := Option.some.inj (selected.symm.trans missingZero_source)
      subst rule
      exact missingZero_sound substitution goal premises head body holds
    · have same := Option.some.inj (selected.symm.trans tailEnd_yes_source)
      subst rule
      exact tailEndYes_sound substitution goal premises head body holds
    · have same := Option.some.inj (selected.symm.trans tailEnd_no_source)
      subst rule
      exact tailEndNo_sound substitution goal premises head body holds
    · have same := Option.some.inj (selected.symm.trans tailCons_source)
      subst rule
      exact tailCons_sound substitution goal premises head body holds
    · have same := Option.some.inj (selected.symm.trans surrogate_source)
      subst rule
      exact surrogate_sound substitution goal premises head body holds
    · have same := Option.some.inj (selected.symm.trans graph_gap_source)
      subst rule
      exact gap_sound substitution goal premises head body holds
    · have same := Option.some.inj (selected.symm.trans noGap_source)
      subst rule
      exact noGap_sound substitution goal premises head body holds
  · have noPrimitive := scan_primitive_has_no_clause selected
    apply PlainBnfExclusionLocalModel.Meaning_of_other goal <;>
      intro name <;> simp_all [scanRelation, scanPrimitiveRelation]

private theorem binary_answer_condition {occurrence : Nat} {relation : String}
    {notLess successor : Bool}
    (shape : (authoredProgram[occurrence]?).bind equationSides? =
      some (binaryShape relation (if notLess then ">=" else "<") successor))
    (left right : Int) {residual : GroundTerm}
    (step : StepAt authoredProgram occurrence [] (binaryCall relation left right) residual)
    (answer : AnswersEval residual [binaryQuery relation left right]) :
    (if notLess then right ≤ (if successor then left + 1 else left)
      else (if successor then left + 1 else left) < right) := by
  have result := (binary_meaning shape left right [binaryQuery relation left right]).mp
    ⟨residual, step, answer⟩
  by_contra denied
  rw [if_neg denied] at result
  cases result

/-- Every successful source provider query preserves the independent partial
observation. The equation inventory ranges over the entire source program. -/
theorem providers_sound : HornProviderGSLT.ProviderSound authoredProgram Model := by
  intro capabilityIndex equationIndex declaration goal residual _selected _capability step answer
  rcases provider_equation_inventory step with
    ⟨rfl, name⟩ | ⟨rfl, name⟩ | ⟨rfl, name⟩ | ⟨rfl, name⟩ |
    ⟨rfl, name⟩ | ⟨rfl, name⟩ | ⟨rfl, name⟩
  · rcases goal with ⟨relation, arguments⟩
    dsimp at name
    subst relation
    unfold Model PlainBnfExclusionLocalModel.Meaning
    split <;> simp_all
    intro leftCanonical rightCanonical same
    have result := (different_meaning leftCanonical rightCanonical _).mp
      ⟨residual, step, answer⟩
    simp [same] at result
  · rcases goal with ⟨relation, arguments⟩
    dsimp at name
    subst relation
    unfold Model PlainBnfExclusionLocalModel.Meaning
    split <;> simp_all
    exact binary_answer_condition (notLess := false) (successor := false)
      less_source _ _ step answer
  · rcases goal with ⟨relation, arguments⟩
    dsimp at name
    subst relation
    unfold Model PlainBnfExclusionLocalModel.Meaning
    split <;> simp_all
    exact binary_answer_condition (notLess := true) (successor := false)
      notLess_source _ _ step answer
  · rcases goal with ⟨relation, arguments⟩
    dsimp at name
    subst relation
    unfold Model PlainBnfExclusionLocalModel.Meaning
    split <;> simp_all
    exact binary_answer_condition (notLess := false) (successor := true)
      IntegerProviders.gap_source _ _ step answer
  · rcases goal with ⟨relation, arguments⟩
    dsimp at name
    subst relation
    unfold Model PlainBnfExclusionLocalModel.Meaning
    split <;> simp_all
    exact binary_answer_condition (notLess := true) (successor := true)
      IntegerProviders.noGap_source _ _ step answer
  · apply PlainBnfExclusionLocalModel.Meaning_of_other goal <;> simp [name]
  · apply PlainBnfExclusionLocalModel.Meaning_of_other goal <;> simp [name]

/-- Any finite terminal source path constrains the complete output term, not
merely outputs already assumed to be Booleans. No trace-safety hypothesis or
prescribed occurrence sequence is required for this direction. -/
theorem exclusion_no_invention {excluded : List Nat} {output : GroundTerm}
    {fuel : Nat} {actions : List HornProviderGSLT.Action}
    (path : HornProviderGSLT.Path authoredProgram actions
      [(fuel, PlainBnfExclusionLocalModel.exclusionGoal excluded output)] []) :
    output = PlainBnfExclusionLocalModel.resultTerm (exceptGapCheck excluded) := by
  exact (PlainBnfExclusionLocalModel.meaning_exclusion_iff excluded output).mp
    (HornProviderGSLT.Path.goal_soundness clauses_sound providers_sound path)

theorem tail_no_invention {previous : Nat} {excluded : List Nat} {output : GroundTerm}
    {fuel : Nat} {actions : List HornProviderGSLT.Action}
    (path : HornProviderGSLT.Path authoredProgram actions
      [(fuel, PlainBnfExclusionLocalModel.tailGoal previous excluded output)] []) :
    output = PlainBnfExclusionLocalModel.resultTerm (gapAfter previous excluded) := by
  exact (PlainBnfExclusionLocalModel.meaning_tail_iff previous excluded output).mp
    (HornProviderGSLT.Path.goal_soundness clauses_sound providers_sound path)

theorem gap_no_invention {previous next : Nat} {excluded : List Nat} {output : GroundTerm}
    {fuel : Nat} {actions : List HornProviderGSLT.Action}
    (path : HornProviderGSLT.Path authoredProgram actions
      [(fuel, PlainBnfExclusionLocalModel.gapGoal previous next excluded output)] []) :
    output = PlainBnfExclusionLocalModel.resultTerm (gapAfter previous (next :: excluded)) := by
  exact (PlainBnfExclusionLocalModel.meaning_gap_iff previous next excluded output).mp
    (HornProviderGSLT.Path.goal_soundness clauses_sound providers_sound path)

theorem exclusion_replay_no_invention {excluded : List Nat} {output : GroundTerm}
    {fuel : Nat} {actions : List HornProviderGSLT.Action}
    (checked : HornProviderGSLT.replayPath? authoredProgram actions
      [(fuel, PlainBnfExclusionLocalModel.exclusionGoal excluded output)] = some []) :
    output = PlainBnfExclusionLocalModel.resultTerm (exceptGapCheck excluded) :=
  exclusion_no_invention (HornProviderGSLT.replayPath?_sound _ _ _ _ checked)

/-- Exact reachable answers of ground source queries. This characterizes
the source relation, not an answer-blind search algorithm or native execution. -/
theorem exclusion_path_iff (excluded : List Nat) (output : GroundTerm) :
    (∃ fuel actions, HornProviderGSLT.Path authoredProgram actions
      [(fuel, PlainBnfExclusionLocalModel.exclusionGoal excluded output)] []) ↔
    output = PlainBnfExclusionLocalModel.resultTerm (exceptGapCheck excluded) := by
  constructor
  · rintro ⟨fuel, actions, path⟩
    exact exclusion_no_invention path
  · intro correct
    subst output
    obtain ⟨actions, _safe, path⟩ := exclusion_execution excluded 0 []
    exact ⟨2 * excluded.length + 2, actions, by
      simpa only [Nat.zero_add, exclusionGoal, resultTerm,
        PlainBnfExclusionLocalModel.exclusionGoal,
        PlainBnfExclusionLocalModel.resultTerm] using path⟩

/-- At the proved sufficient derivation depth, executable replay accepts
some certificate exactly for the independent result. The action sequence
is still supplied evidence, not discovered by this statement. -/
theorem exclusion_replay_iff (excluded : List Nat) (output : GroundTerm) :
    (∃ actions, HornProviderGSLT.replayPath? authoredProgram actions
      [(2 * excluded.length + 2,
        PlainBnfExclusionLocalModel.exclusionGoal excluded output)] = some []) ↔
    output = PlainBnfExclusionLocalModel.resultTerm (exceptGapCheck excluded) := by
  constructor
  · rintro ⟨actions, checked⟩
    exact exclusion_replay_no_invention checked
  · intro correct
    subst output
    obtain ⟨actions, checked⟩ := exclusion_replay excluded 0 []
    exact ⟨actions, by
      simpa only [Nat.zero_add, exclusionGoal, resultTerm,
        PlainBnfExclusionLocalModel.exclusionGoal,
        PlainBnfExclusionLocalModel.resultTerm] using checked⟩

/-- The source relation characterizes Unicode inhabitation only under the
independently required scalar-validity and strict-order hypotheses. -/
theorem exclusion_yes_iff_inhabited (excluded : List Nat)
    (valid : ∀ scalar ∈ excluded,
      Mettapedia.GSLT.Parsing.ParserProfileSemantics.isUnicodeScalar scalar = true)
    (sorted : excluded.Pairwise (· < ·)) :
    (∃ fuel actions, HornProviderGSLT.Path authoredProgram actions
      [(fuel, PlainBnfExclusionLocalModel.exclusionGoal excluded
        (PlainBnfExclusionLocalModel.resultTerm true))] []) ↔
    PlainBnfLexicalInhabitation.InhabitedClass (.except excluded) := by
  rw [exclusion_path_iff, PlainBnfExclusionLocalModel.resultTerm_eq_iff, eq_comm]
  exact PlainBnfLexicalInhabitation.exceptGapCheck_iff excluded valid sorted

/-- This is a rejection of a wrong certificate, not an inference from finite
search exhaustion. It quantifies over every fuel and source action sequence. -/
theorem opposite_exclusion_path_impossible (excluded : List Nat) (fuel : Nat)
    (actions : List HornProviderGSLT.Action) :
    ¬ HornProviderGSLT.Path authoredProgram actions
      [(fuel, PlainBnfExclusionLocalModel.exclusionGoal excluded
        (PlainBnfExclusionLocalModel.resultTerm (!(exceptGapCheck excluded))))] [] := by
  intro path
  have correct := exclusion_no_invention path
  simp only [PlainBnfExclusionLocalModel.resultTerm_eq_iff] at correct
  cases value : exceptGapCheck excluded <;> simp_all

theorem forged_exclusion_path_impossible (excluded : List Nat) (fuel : Nat)
    (actions : List HornProviderGSLT.Action) :
    ¬ HornProviderGSLT.Path authoredProgram actions
      [(fuel, PlainBnfExclusionLocalModel.exclusionGoal excluded (.atom "forged-output"))] [] := by
  intro path
  have correct := exclusion_no_invention path
  cases value : exceptGapCheck excluded <;>
    simp [PlainBnfExclusionLocalModel.resultTerm, value] at correct

theorem full_exclusion_yes_impossible (fuel : Nat) (actions : List HornProviderGSLT.Action) :
    ¬ HornProviderGSLT.Path authoredProgram actions
      [(fuel, PlainBnfExclusionLocalModel.exclusionGoal PlainBnfLexicalInhabitation.allScalars
        (PlainBnfExclusionLocalModel.resultTerm true))] [] := by
  intro path
  have correct := exclusion_no_invention path
  simp [PlainBnfLexicalInhabitation.full_exclusion_gap_check_is_false] at correct

#print axioms exclusion_no_invention
#print axioms tail_no_invention
#print axioms gap_no_invention
#print axioms exclusion_path_iff
#print axioms exclusion_replay_iff
#print axioms exclusion_yes_iff_inhabited
#print axioms opposite_exclusion_path_impossible
#print axioms forged_exclusion_path_impossible
#print axioms full_exclusion_yes_impossible
#print axioms clauses_sound
#print axioms providers_sound

#print axioms provider_equation_inventory
#print axioms no_scan_capability
#print axioms program_protocol_family_inventory
#print axioms program_scan_primitive_heads_absent

#print axioms tail_execution
#print axioms tail_replay
#print axioms exclusion_replay
#print axioms exclusion_inhabitation_realized
#print axioms full_exclusion_negative_replay
#print axioms lowered_exclusion_realized
#print axioms program_scan_family_inventory

end RecursiveExclusion

#print axioms different_meaning
#print axioms tailEnd_replay
#print axioms tailEnd_path_iff
#print axioms tailEnd_scan_agreement
#print axioms different_text_meaning
#print axioms gap_path_iff
#print axioms surrogate_gap_trace_refused

end ProviderExecution

namespace DeclarationCollection

open Mettapedia.GSLT.Parsing.PlainBnfDeclarationSource

local macro "qualify_declaration_row" : tactic => `(tactic| (
  rw [elaborateProgram?_getElem? authoredProgram_source_exact]
  simp only [compositionRewrites, authoredSources, List.flatMap_cons, List.flatMap_nil]
  simp only [List.getElem?_append,
    (source_field_lengths groundRelationsProgram_decodes).2.2.2,
    (source_field_lengths cettaPettaGroundRelationsProgram_decodes).2.2.2,
    (source_field_lengths groundIntegerRelationsProgram_decodes).2.2.2,
    (source_field_lengths cettaPettaGroundIntegerRelationsProgram_decodes).2.2.2,
    (source_field_lengths semanticAdmissionProgram_decodes).2.2.2,
    (source_field_lengths graphAnalysisProgram_decodes).2.2.2,
    Nat.reduceLT, Nat.reduceSub, ↓reduceIte]
  rw [rawRewriteAt?_of_decode semanticAdmissionProgram_decodes]
  dsimp only [semanticAdmissionProgram, rawRewriteAt?, List.getElem?_cons_succ,
    List.getElem?_cons_zero, Option.bind_some, decodeRewrite]
  decide +kernel))

private theorem declaration_row_0 :
    authoredProgram[14]? = declarationRules[0]? := by
  qualify_declaration_row

private theorem declaration_row_1 :
    authoredProgram[15]? = declarationRules[1]? := by
  qualify_declaration_row

private theorem declaration_row_2 :
    authoredProgram[16]? = declarationRules[2]? := by
  qualify_declaration_row

private theorem declaration_row_3 :
    authoredProgram[17]? = declarationRules[3]? := by
  qualify_declaration_row

private theorem declaration_row_4 :
    authoredProgram[18]? = declarationRules[4]? := by
  qualify_declaration_row

private theorem declaration_row_5 :
    authoredProgram[19]? = declarationRules[5]? := by
  qualify_declaration_row

private theorem declaration_row_6 :
    authoredProgram[20]? = declarationRules[6]? := by
  qualify_declaration_row

private theorem declaration_row_7 :
    authoredProgram[21]? = declarationRules[7]? := by
  qualify_declaration_row

private theorem declaration_row_8 :
    authoredProgram[22]? = declarationRules[8]? := by
  qualify_declaration_row

private theorem declaration_row_9 :
    authoredProgram[23]? = declarationRules[9]? := by
  qualify_declaration_row

private theorem declaration_row_10 :
    authoredProgram[24]? = declarationRules[10]? := by
  qualify_declaration_row

private theorem declaration_row_11 :
    authoredProgram[25]? = declarationRules[11]? := by
  qualify_declaration_row

private theorem declaration_row_12 :
    authoredProgram[26]? = declarationRules[12]? := by
  qualify_declaration_row

/-- All thirteen declaration rules are the actual ordered source occurrences,
including the disequality guard and every threaded output variable. -/
theorem declaration_rules_source : HasDeclarationRules authoredProgram 14 := by
  intro index
  fin_cases index
  · exact declaration_row_0
  · exact declaration_row_1
  · exact declaration_row_2
  · exact declaration_row_3
  · exact declaration_row_4
  · exact declaration_row_5
  · exact declaration_row_6
  · exact declaration_row_7
  · exact declaration_row_8
  · exact declaration_row_9
  · exact declaration_row_10
  · exact declaration_row_11
  · exact declaration_row_12

#print axioms declaration_rules_source

/-- This examines all 174 source occurrences, not only the selected thirteen. -/
theorem declaration_family_inventory :
    authoredProgram.map (fun rule => declarationRelation rule.head.relation) =
      List.replicate 14 false ++ List.replicate 13 true ++ List.replicate 147 false := by
  rw [ProviderExecution.RecursiveExclusion.headObservation_exact]
  unfold authoredSources
  rw [source_eq_of_decode groundRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundRelationsProgram_decodes,
    source_eq_of_decode groundIntegerRelationsProgram_decodes,
    source_eq_of_decode cettaPettaGroundIntegerRelationsProgram_decodes,
    source_eq_of_decode semanticAdmissionProgram_decodes,
    source_eq_of_decode graphAnalysisProgram_decodes]
  decide +kernel

theorem only_declaration_rules : OnlyDeclarationRules authoredProgram 14 := by
  intro index rule selected belongs
  have marked :
      (authoredProgram.map (fun row => declarationRelation row.head.relation))[index]? =
        some true := by simp [List.getElem?_map, selected, belongs]
  rw [declaration_family_inventory] at marked
  simp only [List.getElem?_append, List.length_append, List.length_replicate,
    List.getElem?_replicate] at marked
  split_ifs at marked <;> simp_all

/-- No foreign provider can discharge a declaration-pass goal. -/
theorem no_declaration_capability {index : Nat} {rule : Rule} {goal : GroundAtom}
    (selected : authoredProgram[index]? = some rule)
    (belongs : declarationRelation goal.relation = true) :
    ¬ Mettapedia.GSLT.Parsing.HornProviderGSLT.CapabilityDeclaration rule goal := by
  intro capability
  have names := ProviderExecution.RecursiveExclusion.capability_relation_inventory
    selected capability
  simp only [List.mem_cons, List.not_mem_nil, or_false] at names
  rcases names with name | name | name | name | name | name | name <;>
    simp [declarationRelation, name] at belongs

#print axioms declaration_family_inventory
#print axioms only_declaration_rules
#print axioms no_declaration_capability

open Mettapedia.GSLT.Parsing
open PlainBnfDeclarationSemantics PlainBnfDeclarationRealization PlainBnfDeclarationEncoding

/-- Discharge the realization's primitive hypothesis using the actual authored
capability and equation, not an assumed successful guard. -/
theorem different_realizes : DifferentRealizes authoredProgram := by
  intro left right leftCanonical rightCanonical unequal
  have checked := ProviderExecution.different_query_replay leftCanonical rightCanonical 0 []
  simp only [if_neg unequal] at checked
  refine ⟨1, [.provider 0 1], ?_⟩
  exact .cons (HornProviderGSLT.replayStep?_sound _ _ _ _ checked) (.nil [])

/-- All finite canonical declaration inputs have an actual authored source
path to their independently specified ordered result. This is source-path
existence, not yet a theorem about native answer enumeration. -/
theorem declaration_collection_all_inputs
    (entries : List (Entry GroundTerm GroundTerm GroundTerm GroundTerm))
    (before : List (Definition GroundTerm GroundTerm GroundTerm))
    (canonicalEntries : CanonicalEntries entries) (canonicalBefore : CanonicalDefinitions before) :
    Realizable authoredProgram
      (collectGoal entries before (collect entries before).1 (collect entries before).2) :=
  collect_result_realizable declaration_rules_source different_realizes entries before
    canonicalEntries canonicalBefore

#print axioms different_realizes
#print axioms declaration_collection_all_inputs
#print axioms PlainBnfDeclarationSemantics.collectTotalUnique
#print axioms PlainBnfDeclarationSemantics.Collect.unique

/-- The canonicalizing primitive cannot also be implemented by an ordinary
clause elsewhere in the selected source composition. -/
theorem different_has_no_clause {index : Nat} {rule : Rule}
    (selected : authoredProgram[index]? = some rule) :
    rule.head.relation ≠ "different" := by
  have absent := ProviderExecution.RecursiveExclusion.scan_primitive_has_no_clause selected
  intro equal
  simp [ProviderExecution.RecursiveExclusion.scanPrimitiveRelation, equal] at absent

/-- Any successful authored provider occurrence for a canonical pair denotes
actual disequality, not merely success of the preferred replay equation. -/
theorem different_provider_no_invention {capabilityIndex equationIndex : Nat}
    {declaration : Rule} {left right residual : GroundTerm}
    (selected : authoredProgram[capabilityIndex]? = some declaration)
    (capability : HornProviderGSLT.CapabilityDeclaration declaration
      (PlainBnfDeclarationRealization.differentGoal left right))
    (step : HornEquationContextual.StepAt authoredProgram equationIndex []
      (HornProviderGSLT.providerCall
        (PlainBnfDeclarationRealization.differentGoal left right)) residual)
    (answer : HornIntegerProvider.AnswersEval residual
      [HornProviderGSLT.queryTerm (PlainBnfDeclarationRealization.differentGoal left right)])
    (leftCanonical : HornIntegerProvider.AliasFree left)
    (rightCanonical : HornIntegerProvider.AliasFree right) : left ≠ right := by
  have sound := ProviderExecution.RecursiveExclusion.providers_sound
    capabilityIndex equationIndex declaration
    (PlainBnfDeclarationRealization.differentGoal left right) residual
    selected capability step answer
  exact sound leftCanonical rightCanonical

#print axioms different_has_no_clause
#print axioms different_provider_no_invention
#print axioms PlainBnfDeclarationSemantics.Collect.fresh_empty_occurrences_closed

open PlainBnfDeclarationReflection

theorem no_different_clauses : NoDifferentRules authoredProgram := by
  intro index rule selected
  exact different_has_no_clause selected

theorem no_declaration_providers : NoDeclarationProviders authoredProgram := by
  intro index rule goal selected capability
  cases marked : declarationRelation goal.relation with
  | false => rfl
  | true => exact (no_declaration_capability selected marked capability).elim

theorem different_providers_sound : DifferentProviderSound authoredProgram := by
  intro capabilityIndex equationIndex declaration goal residual selected capability step answer name
  rcases goal with ⟨relation, arguments⟩
  dsimp at name
  subst relation
  intro left right equal leftCanonical rightCanonical
  change arguments = GroundTerms.ofList [left, right] at equal
  subst arguments
  exact different_provider_no_invention selected capability step answer leftCanonical rightCanonical

/-- The complete source program has exactly the independently specified
reachable output values for every canonical typed input. This does not count
native answer occurrences or claim uniqueness of padded action certificates. -/
theorem declaration_collection_path_iff
    (entries : List (Entry GroundTerm GroundTerm GroundTerm GroundTerm))
    (before : List (Definition GroundTerm GroundTerm GroundTerm))
    (after diagnostics : GroundTerm)
    (canonicalEntries : CanonicalEntries entries) (canonicalBefore : CanonicalDefinitions before) :
    Realizable authoredProgram
      ⟨"BNFCollectDefinitionsV1", GroundTerms.ofList
        [encodeEntries entries, encodeDefinitions before, after, diagnostics]⟩ ↔
    after = encodeDefinitions (collect entries before).1 ∧
    diagnostics = encodeDiagnostics (collect entries before).2 := by
  constructor
  · rintro ⟨fuel, actions, path⟩
    exact collect_path_reflects_outputs declaration_rules_source only_declaration_rules
      no_different_clauses no_declaration_providers different_providers_sound
      entries before after diagnostics canonicalEntries canonicalBefore path
  · rintro ⟨rfl, rfl⟩
    exact declaration_collection_all_inputs entries before canonicalEntries canonicalBefore

/-- A duplicate declaration cannot be silently accepted without its ordered
diagnostic, even by a trace selecting arbitrary occurrences in the source. -/
theorem duplicate_diagnostic_cannot_be_dropped
    (name expression firstSpan duplicateSpan : GroundTerm)
    (canonicalName : HornIntegerProvider.AliasFree name) (after : GroundTerm) :
    ¬ Realizable authoredProgram
      ⟨"BNFCollectDefinitionsV1", GroundTerms.ofList
        [encodeEntries [.rule name expression firstSpan, .rule name expression duplicateSpan],
          encodeDefinitions [], after, encodeDiagnostics []]⟩ := by
  have canonical : CanonicalEntries
      [.rule name expression firstSpan, .rule name expression duplicateSpan] := by
    intro entry member
    simp only [List.mem_cons, List.not_mem_nil, or_false] at member
    rcases member with rfl | rfl <;> exact CanonicalEntry.rule _ _ canonicalName
  intro path
  have output := (declaration_collection_path_iff _ [] after (encodeDiagnostics []) canonical
    (by intro definition member; cases member)).mp path
  simp [collect, definitionStep, lookup, encodeDiagnostics] at output

/-- Untyped junk cannot be produced as a definition-list output of a typed
empty input. This is a reverse boundary, not a selected-output-only check. -/
theorem malformed_definition_output_refused (diagnostics : GroundTerm) :
    ¬ Realizable authoredProgram
      ⟨"BNFCollectDefinitionsV1", GroundTerms.ofList
        [encodeEntries [], encodeDefinitions [], .integer 7, diagnostics]⟩ := by
  intro path
  have output := (declaration_collection_path_iff [] [] (.integer 7) diagnostics
    (by intro entry member; cases member) (by intro definition member; cases member)).mp path
  simp [collect, encodeDefinitions] at output

#print axioms declaration_collection_path_iff
#print axioms duplicate_diagnostic_cannot_be_dropped
#print axioms malformed_definition_output_refused

open PlainBnfDeclarationOccurrences

/-- The actual source composition fixes both the result and the ordered
declaration-append occurrences. There are no caller-supplied source-inventory
hypotheses. This remains a source observation, not native answer enumeration. -/
theorem append_definition_source_observation_iff
    (before : List (Definition GroundTerm GroundTerm GroundTerm))
    (definition : Definition GroundTerm GroundTerm GroundTerm) (output : GroundTerm)
    (trace : List (Sum Nat (Nat × Nat))) :
    (∃ fuel actions, HornProviderGSLT.Path authoredProgram actions
      [(fuel, appendDefinitionInput before definition output)] [] ∧
      occurrenceTrace actions = trace) ↔
    output = encodeDefinitions (before ++ [definition]) ∧
      trace = List.replicate before.length (.inl 18) ++ [.inl 17] := by
  exact append_definition_observation_iff declaration_rules_source only_declaration_rules
    no_declaration_providers before definition output trace

/-- Equal diagnostic values retain their separate positions. The exact source
trace contains one recursive occurrence for each left-list position. -/
theorem append_diagnostics_source_observation_iff
    (left right : List (Diagnostic GroundTerm GroundTerm)) (output : GroundTerm)
    (trace : List (Sum Nat (Nat × Nat))) :
    (∃ fuel actions, HornProviderGSLT.Path authoredProgram actions
      [(fuel, appendDiagnosticsInput left right output)] [] ∧
      occurrenceTrace actions = trace) ↔
    output = encodeDiagnostics (left ++ right) ∧
      trace = List.replicate left.length (.inl 20) ++ [.inl 19] := by
  exact append_diagnostics_observation_iff declaration_rules_source only_declaration_rules
    no_declaration_providers left right output trace

/-- The bound counts all actual source actions; no provider actions are
discarded and no native allocation or wall-time bound is asserted. -/
theorem append_definition_source_actions_exact
    (before : List (Definition GroundTerm GroundTerm GroundTerm))
    (definition : Definition GroundTerm GroundTerm GroundTerm) (output : GroundTerm)
    {fuel : Nat} {actions : List HornProviderGSLT.Action}
    (path : HornProviderGSLT.Path authoredProgram actions
      [(fuel, appendDefinitionInput before definition output)] []) :
    actions.length = before.length + 1 :=
  append_definition_source_action_count declaration_rules_source only_declaration_rules
    no_declaration_providers before definition output path

theorem append_diagnostics_source_actions_exact
    (left right : List (Diagnostic GroundTerm GroundTerm)) (output : GroundTerm)
    {fuel : Nat} {actions : List HornProviderGSLT.Action}
    (path : HornProviderGSLT.Path authoredProgram actions
      [(fuel, appendDiagnosticsInput left right output)] []) :
    actions.length = left.length + 1 :=
  append_diagnostics_source_action_count declaration_rules_source only_declaration_rules
    no_declaration_providers left right output path

/-- Positive control: two equal diagnostics survive as two values and two
recursive source occurrences, followed by the base occurrence. -/
theorem repeated_diagnostics_source_observation
    (diagnostic : Diagnostic GroundTerm GroundTerm) :
    ∃ fuel actions, HornProviderGSLT.Path authoredProgram actions
      [(fuel, appendDiagnosticsInput [diagnostic, diagnostic] []
        (encodeDiagnostics [diagnostic, diagnostic]))] [] ∧
      occurrenceTrace actions = [.inl 20, .inl 20, .inl 19] :=
  (append_diagnostics_source_observation_iff _ _ _ _).mpr ⟨rfl, rfl⟩

/-- A trace that coalesces two equal diagnostics into one recursive source
occurrence cannot be a successful execution, even with an arbitrary output. -/
theorem coalesced_diagnostic_occurrence_refused
    (diagnostic : Diagnostic GroundTerm GroundTerm) (output : GroundTerm) :
    ¬ (∃ fuel actions, HornProviderGSLT.Path authoredProgram actions
      [(fuel, appendDiagnosticsInput [diagnostic, diagnostic] [] output)] [] ∧
      occurrenceTrace actions = [.inl 20, .inl 19]) := by
  intro observed
  have exactTrace :=
    ((append_diagnostics_source_observation_iff _ _ _ _).mp observed).2
  have lengthEq := congrArg List.length exactTrace
  simp at lengthEq

/-- A real provider coordinate from this source cannot replace the authored
declaration-append clause. The complete capability inventory rules it out. -/
theorem provider_cannot_replace_append_occurrence
    (definition : Definition GroundTerm GroundTerm GroundTerm) (output : GroundTerm) :
    ¬ (∃ fuel actions, HornProviderGSLT.Path authoredProgram actions
      [(fuel, appendDefinitionInput [] definition output)] [] ∧
      occurrenceTrace actions = [.inr (0, 1)]) := by
  intro observed
  have exactTrace :=
    ((append_definition_source_observation_iff _ _ _ _).mp observed).2
  simp at exactTrace

#print axioms append_definition_source_observation_iff
#print axioms append_diagnostics_source_observation_iff
#print axioms append_definition_source_actions_exact
#print axioms append_diagnostics_source_actions_exact
#print axioms repeated_diagnostics_source_observation
#print axioms coalesced_diagnostic_occurrence_refused
#print axioms provider_cannot_replace_append_occurrence

end DeclarationCollection

end Cetta.PlainBnf.AuthoredSourceQualificationV1
