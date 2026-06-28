!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(assertEqual (mm-db-summary (mm-core-local-var-good-path)) (mm-core-local-var-good-summary))
