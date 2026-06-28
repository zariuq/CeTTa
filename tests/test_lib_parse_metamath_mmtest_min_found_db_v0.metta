!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(assertEqual (mm-db-summary (mm-mmtest-min-found-path)) (mm-mmtest-min-found-summary))
