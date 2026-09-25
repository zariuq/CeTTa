import Mettapedia.GSLT.Parsing.TptpCorpusCompatibilitySource
import Mettapedia.GSLT.Parsing.TptpOfficialCompositionSource
import Mettapedia.GSLT.Parsing.TptpCompactFirstOrderProjectionSource
import Mettapedia.GSLT.Parsing.TptpCanonicalPrintSource

/-!
# Qualification of the authored TPTP reader transformations

This client quotes the exact CeTTa sources used by the reader and discharges
the source contracts proved by the reusable Mettapedia models.  It covers the
official-row partition, ordered first-order view, canonical printer, and the
compatibility transform's NativeType request and consumption.

The structural models are direct typed functions rather than Horn encodings.
Native C remains outside these theorems and is checked by the pinned-snapshot
gates.
-/

set_option autoImplicit false

namespace Cetta.Tptp.ReaderSourceQualificationV1

open Algorithms.MeTTa.Simple.Parser (SExpr)
open scoped Mettapedia.OSLF.MeTTaIL.MeTTaSyntaxQuotation

private def lexicalSyntax : SExpr :=
  metta_sexpr_file% petta "../../../langdef/tptp/official_lexical_to_ebnf_v1.metta"

private def syntaxSyntax : SExpr :=
  metta_sexpr_file% petta "../../../langdef/tptp/official_syntax_to_ebnf_v1.metta"

private def firstOrderProjectionSyntax : SExpr :=
  metta_sexpr_file% petta "../../../langdef/tptp/compact_records_to_first_order_v1.metta"

private def compatibilityNativeTypeSyntax : SExpr :=
  metta_sexpr_file% petta "../../../langdef/tptp/official_corpus_compatibility_native_types_v1.metta"

private def compatibilitySyntax : SExpr :=
  metta_sexpr_file% petta "../../../langdef/tptp/official_corpus_compatibility_v1.metta"

set_option maxRecDepth 100000 in
private def canonicalPrintSyntax : SExpr :=
  metta_sexpr_file% petta "../../../langdef/tptp/official_records_to_text_v1.metta"

theorem official_composition_source_exact :
    Mettapedia.GSLT.Parsing.TptpOfficialCompositionSource.AuthoredCompositionSourceExact
      lexicalSyntax syntaxSyntax := by
  unfold Mettapedia.GSLT.Parsing.TptpOfficialCompositionSource.AuthoredCompositionSourceExact
  decide +kernel

theorem compact_first_order_projection_source_exact :
    Mettapedia.GSLT.Parsing.TptpCompactFirstOrderProjectionSource.AuthoredProjectionSourceExact
      firstOrderProjectionSyntax := by
  unfold Mettapedia.GSLT.Parsing.TptpCompactFirstOrderProjectionSource.AuthoredProjectionSourceExact
  decide +kernel

theorem compatibility_native_type_source_exact :
    Mettapedia.GSLT.Parsing.TptpCorpusCompatibilitySource.AuthoredCompatibilitySourceExact
      compatibilityNativeTypeSyntax compatibilitySyntax := by
  unfold Mettapedia.GSLT.Parsing.TptpCorpusCompatibilitySource.AuthoredCompatibilitySourceExact
  decide +kernel

set_option maxRecDepth 100000 in
theorem canonical_print_source_exact :
    Mettapedia.GSLT.Parsing.TptpCanonicalPrintSource.AuthoredCanonicalPrintSourceExact
      canonicalPrintSyntax := by
  unfold Mettapedia.GSLT.Parsing.TptpCanonicalPrintSource.AuthoredCanonicalPrintSourceExact
  simp only
    [Mettapedia.GSLT.Parsing.TptpOfficialCompositionSource.app,
     Mettapedia.GSLT.Parsing.TptpCanonicalPrintSource.sourceString,
     Mettapedia.GSLT.Parsing.TptpCanonicalPrintSource.printerInputLeft,
     Mettapedia.GSLT.Parsing.TptpCanonicalPrintSource.printerAnnotatedRight,
     Mettapedia.GSLT.Parsing.TptpCanonicalPrintSource.printerClauseRight,
     Mettapedia.GSLT.Parsing.TptpCanonicalPrintSource.printerBinaryLeft,
     Mettapedia.GSLT.Parsing.TptpCanonicalPrintSource.printerBinaryRight,
     Mettapedia.GSLT.Parsing.TptpCanonicalPrintSource.printerQuantifierLeft,
     Mettapedia.GSLT.Parsing.TptpCanonicalPrintSource.printerQuantifierRight]
  repeat' apply And.intro
  all_goals
    unfold canonicalPrintSyntax
    unfold Mettapedia.GSLT.Parsing.TptpOfficialCompositionSource.equation?
    unfold Mettapedia.GSLT.LanguageDef.CanonicalSourceGSLT.rawRewriteAt?
    unfold Mettapedia.GSLT.LanguageDef.CanonicalSourceGSLT.decodeRewrite
    unfold Mettapedia.GSLT.LanguageDef.CanonicalSourceGSLT.atomToken?
    simp

#print axioms official_composition_source_exact
#print axioms compact_first_order_projection_source_exact
#print axioms compatibility_native_type_source_exact
#print axioms canonical_print_source_exact

end Cetta.Tptp.ReaderSourceQualificationV1
