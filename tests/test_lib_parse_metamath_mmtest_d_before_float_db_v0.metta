!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(assertEqual (mm-db-summary (mm-mmtest-d-before-float-path)) (mm-mmtest-d-before-float-summary))
