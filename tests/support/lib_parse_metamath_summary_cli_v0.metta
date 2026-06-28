!(import! &self ./lib_parse_metamath_grammar_v0.metta)
!(import! &self system)

!(println! (mm-db-summary (system:arg 0)))
