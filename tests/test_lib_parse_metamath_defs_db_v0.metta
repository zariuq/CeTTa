!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(println! (mm-db-parse-class (mm-demo0-defs-path)))
