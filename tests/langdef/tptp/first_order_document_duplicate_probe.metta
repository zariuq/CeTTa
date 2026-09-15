!(import! &self langdef)

!(println!
   (TptpFirstOrderDocumentDuplicateProbe
     (__cetta_lib_deterministic_equations_load
       (LangDef:EquationSources
         "langdef/tptp/cnf_clause_data_bridge_v1.metta"
         "langdef/tptp/first_order_document_bridge_v1.metta"
         "tests/langdef/tptp/first_order_document_duplicate_iff_v1.metta"))))
