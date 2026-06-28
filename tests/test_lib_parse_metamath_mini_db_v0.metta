!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(assertEqual (mm-db-parse-class (mm-mini-db-path)) Unique)
!(assertEqual (mm-db-summary (mm-mini-db-path)) (mm-mini-db-summary))
