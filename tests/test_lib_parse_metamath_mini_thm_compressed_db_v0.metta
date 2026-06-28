!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(assertEqual (mm-db-summary (mm-mini-thm-compressed-path)) (mm-mini-thm-compressed-summary))
