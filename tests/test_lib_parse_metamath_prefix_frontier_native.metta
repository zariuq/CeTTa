!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(assertEqual
  (mm-db-lex-count "tests/support/metamath_demo0_theorem_v0.mm")
  166)

!(assertEqual
  (mm-db-lex-count "tests/support/metamath_demo0_block_v0.mm")
  124)

!(assertEqual
  (mm-db-prefix-class "tests/support/metamath_demo0_theorem_v0.mm" 124)
  Unique)

!(assertEqual
  (mm-db-prefix-class "tests/support/metamath_demo0_theorem_v0.mm" 125)
  NoParse)

!(assertEqual
  (mm-db-lex-count "tests/support/metamath_demo0_theorem_noblock_v0.mm")
  143)

!(assertEqual
  (mm-db-lex-count "tests/support/metamath_demo0_axioms_v0.mm")
  103)

!(assertEqual
  (mm-db-prefix-class "tests/support/metamath_demo0_theorem_noblock_v0.mm" 103)
  Unique)

!(assertEqual
  (mm-db-prefix-class "tests/support/metamath_demo0_theorem_noblock_v0.mm" 104)
  NoParse)
