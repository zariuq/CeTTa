!(import! &self ./lib_parse_metamath_grammar_v0.metta)
!(import! &self system)

!(println!
  (case (mm-lex-file (system:arg 0))
    (((Ok $toks) LexOk)
     ($o $o))))
