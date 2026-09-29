import Mettapedia.GSLT.Parsing.TptpCorpusCompatibilitySource
import Mettapedia.GSLT.Parsing.TptpOfficialCompositionSource

/-!
# Qualification of the authored TPTP reader transformations

This client quotes the exact CeTTa sources used by the reader and discharges
the source contracts proved by the reusable Mettapedia models.  It covers the
official-row partition and the compatibility transform's NativeType request
and consumption.

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

private def compatibilityNativeTypeSyntax : SExpr :=
  metta_sexpr_file% petta "../../../langdef/tptp/official_corpus_compatibility_native_types_v1.metta"

private def compatibilitySyntax : SExpr :=
  metta_sexpr_file% petta "../../../langdef/tptp/official_corpus_compatibility_v1.metta"

theorem official_composition_source_exact :
    Mettapedia.GSLT.Parsing.TptpOfficialCompositionSource.AuthoredCompositionSourceExact
      lexicalSyntax syntaxSyntax := by
  unfold Mettapedia.GSLT.Parsing.TptpOfficialCompositionSource.AuthoredCompositionSourceExact
  decide +kernel

theorem compatibility_native_type_source_exact :
    Mettapedia.GSLT.Parsing.TptpCorpusCompatibilitySource.AuthoredCompatibilitySourceExact
      compatibilityNativeTypeSyntax compatibilitySyntax := by
  unfold Mettapedia.GSLT.Parsing.TptpCorpusCompatibilitySource.AuthoredCompatibilitySourceExact
  decide +kernel

#print axioms official_composition_source_exact
#print axioms compatibility_native_type_source_exact

end Cetta.Tptp.ReaderSourceQualificationV1
