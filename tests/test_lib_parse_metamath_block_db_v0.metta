!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(assertEqual (mm-db-summary (mm-demo0-block-path)) (mm-demo0-block-summary))
