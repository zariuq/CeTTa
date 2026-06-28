!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(assertEqual (mm-db-summary (mm-mmtest-compressed-z-path)) (mm-mmtest-compressed-z-summary))
