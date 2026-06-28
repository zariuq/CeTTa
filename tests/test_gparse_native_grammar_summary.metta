!(import! &self gparse)

!(assertEqualToResult
  (gparse:grammar-summary (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta") mm-db-g)
  ((GParseGrammarSummary 4 0 20 0)))

!(assertEqualToResult
  (gparse:grammar-summary (gparse:repo-path "tests/test_lib_parse_binding_regression.metta") g-lam)
  ((GParseGrammarSummary 0 3 1 1)))

!(assertEqualToResult
  (gparse:grammar-summary (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta") rho-g)
  ((GParseGrammarSummary 0 11 7 1)))

!(assertEqualToResult
  (gparse:grammar-summary (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta") nope)
  ((Error __cetta_lib_gparse_grammar_summary "definition nope not found")))
