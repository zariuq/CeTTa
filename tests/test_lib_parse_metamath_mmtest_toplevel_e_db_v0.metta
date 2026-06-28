!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(assertEqual (mm-db-summary (mm-mmtest-toplevel-e-path)) (mm-mmtest-toplevel-e-summary))
