!(import! &self ./lib_parse_metamath_grammar_v0.metta)
!(import! &self system)

!(println! "STAGE lex")
!(println!
  (case (mm-lex-file (system:arg 0))
    (((Ok $toks) (mm-count $toks))
     ($o $o))))

!(println! "STAGE parse-class")
!(println! (mm-db-parse-class (system:arg 0)))
