!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(println! (case (mm-lex-file (mm-mini-db-path)) (((Ok $toks) (lp-parse-shared (mm-db-g) db $toks)) ($o bad-lex))))
