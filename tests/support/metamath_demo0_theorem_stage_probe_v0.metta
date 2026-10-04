!(import! &self ./lib_parse_metamath_grammar_v0.metta)

!(println! "STAGE lex")
!(println!
  (case (mm-lex-file "tests/support/metamath_demo0_theorem_v0.mm")
    (((Ok $toks) (mm-count $toks))
     ($o $o))))

!(println! "STAGE parse-class")
!(println! (mm-db-parse-class "tests/support/metamath_demo0_theorem_v0.mm"))
