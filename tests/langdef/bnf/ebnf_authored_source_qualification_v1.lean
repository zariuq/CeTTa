import Mettapedia.GSLT.Parsing.EbnfWidthSource

/-!
# Fresh-file authentication for the selected EBNF source theorems

Run this client directly, even when its imported modules are cached. The
quotations below read today's authored files and compare them with the source
snapshots used by the imported proofs. This check is source authentication,
not a replacement for the semantic theorems or a whole-runtime proof.
-/

set_option autoImplicit false

namespace Cetta.Ebnf.AuthoredSourceQualificationV1

open Algorithms.MeTTa.Simple.Parser (SExpr)
open Mettapedia.GSLT.Parsing
open scoped Mettapedia.OSLF.MeTTaIL.MeTTaSyntaxQuotation

def freshLowering : SExpr :=
  metta_sexpr_file% petta "../../../langdef/bnf/ebnf_lowering_v1.metta"

def freshProjection : SExpr :=
  metta_sexpr_file% petta "../../../langdef/bnf/ebnf_derivation_projection_v1.metta"

theorem fresh_lowering : freshLowering = EbnfRepetitionSource.loweringSyntax := rfl
theorem fresh_projection : freshProjection = EbnfRepetitionSource.projectionSyntax := rfl

#print axioms fresh_lowering
#print axioms fresh_projection
#print axioms EbnfHelperNameSource.helperText_injective
#print axioms EbnfHelperNameSource.helperText_not_existing
#print axioms EbnfHelperNameSource.counter_one_source
#print axioms EbnfHelperNameSource.allocate_group_source
#print axioms EbnfHelperNameSource.install_source
#print axioms EbnfRepetitionSource.source_accumulator_preserves_spine
#print axioms EbnfWidthSource.source_width_preserved
#print axioms EbnfWidthSource.lower_document_uses_scan
#print axioms EbnfWidthSource.text_width_path
#print axioms EbnfWidthSource.max_path
#print axioms EbnfWidthSource.scan_components_path
#print axioms EbnfWidthSource.nested_reference_path
#print axioms EbnfWidthSource.helper_fresh_after_scan
#print axioms EbnfWidthSource.cannot_return_short_width

end Cetta.Ebnf.AuthoredSourceQualificationV1
