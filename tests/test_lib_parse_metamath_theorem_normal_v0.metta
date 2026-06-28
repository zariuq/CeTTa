!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(assertEqual (lp-parse-class (mm-g) stmt (mm-normal-thm-toks)) Unique)
!(assertEqual
  (lp-print (mm-g) (pu-tree (lp-parse (mm-g) stmt (mm-normal-thm-toks))))
  (Ok (mm-normal-thm-toks)))
