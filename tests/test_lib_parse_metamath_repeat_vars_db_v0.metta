!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(assertEqual (mm-db-summary (mm-repeat-vars-path)) (mm-repeat-vars-summary))
