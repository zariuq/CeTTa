!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(assertEqual (mm-db-parse-class (mm-a1-block-pair-path)) Unique)
!(assertEqual (mm-db-summary (mm-a1-block-pair-path)) (mm-a1-block-pair-summary))
