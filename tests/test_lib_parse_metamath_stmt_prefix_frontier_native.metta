!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(assertEqual
  (mm-stmt-lex-count (mm-demo0-theorem-stmt-raw))
  42)

!(assertEqual
  (mm-stmt-prefix-class (mm-demo0-theorem-stmt-raw) 1)
  NoParse)

!(assertEqual
  (mm-stmt-prefix-class (mm-demo0-theorem-stmt-raw) 41)
  NoParse)

!(assertEqual
  (mm-stmt-prefix-class (mm-demo0-theorem-stmt-raw) 42)
  Unique)

!(assertEqual
  (mm-stmt-lex-count (mm-demo0-theorem-noblock-stmt-raw))
  40)

!(assertEqual
  (mm-stmt-prefix-class (mm-demo0-theorem-noblock-stmt-raw) 1)
  NoParse)

!(assertEqual
  (mm-stmt-prefix-class (mm-demo0-theorem-noblock-stmt-raw) 39)
  NoParse)

!(assertEqual
  (mm-stmt-prefix-class (mm-demo0-theorem-noblock-stmt-raw) 40)
  Unique)
