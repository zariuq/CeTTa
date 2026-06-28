!(import! &self gparse)

!(assertEqualToResult
  (gparse:slr-summary (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta") mm-db-g db)
  ((GParseSLRSummary 43 44 40 39 1 0)))

!(assertEqualToResult
  (gparse:slr-summary (gparse:repo-path "tests/test_lib_parse_binding_regression.metta") g-lam term)
  ((GParseSLRSummary 9 13 3 8 1 0)))

!(assertEqualToResult
  (gparse:slr-summary (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta") rho-g proc)
  ((GParseSLRSummary 39 67 14 95 1 1)))

!(assertEqualToResult
  (gparse:slr-summary (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta") rho-g-ambig proc)
  ((GParseSLRSummary 5 4 2 3 1 1)))

!(assertEqualToResult
  (gparse:slr-summary (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta") mm-db-g nope)
  ((Error __cetta_lib_gparse_slr_summary "start nonterminal not present in grammar")))
