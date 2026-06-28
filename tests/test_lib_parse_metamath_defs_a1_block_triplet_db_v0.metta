!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(assertEqual (mm-db-parse-class (mm-demo0-defs-a1-block-triplet-path)) Unique)
!(assertEqual (mm-db-summary (mm-demo0-defs-a1-block-triplet-path)) (mm-demo0-defs-a1-block-triplet-summary))
