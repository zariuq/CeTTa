(gslt-langdef-v1
  (name TptpFofCnfV1)
  (source "../../experiments/gslt2parse_foundation/presentations/core/syntax_core_v1.metta")
  (source "../../experiments/gslt2parse_foundation/presentations/shared/lookahead_core_v1.metta")
  (source "../../experiments/gslt2parse_foundation/presentations/shared/char_core_v1.metta")
  (source "../../experiments/gslt2parse_foundation/presentations/shared/ground_relations_v1.metta")
  (source "unicode_scalar_classes_v1.metta")
  (source "syntax_fof_cnf_v1.metta")
  (start (pp-def tptp-file))
  (parser-pack "generated/parser_pack_v1.abi")
  (lock "generated/langdef_lock_v1.metta")
  (program-source "first_order_document_composition_v1.metta")
  (program "generated/first_order_document_pipeline_v1.generated.metta")
  (import-entry tptp-doc:parse->import)
  (extension-source "cnf_clause_data_bridge_v1.metta")
  (extension-source "first_order_document_bridge_v1.metta")
  (extension-artifact "source-language-def-v1"
    "fof_cnf_syntax_tree_v1.metta")
  (source-category "SyntaxTree")
  (extension-artifact "clause-data-result-language-def-v1"
    "../logic/first_order_clause_data_v1.metta")
  (extension-artifact "result-language-def-v1"
    "../logic/tptp_first_order_document_v1.metta")
  (result-category "Document"))
