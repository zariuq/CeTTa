!(import! &self gparse)
!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self ./support/rhocalc_lib_parse_translator_v3.metta)
!(import! &self ./support/lib_parse_generalized_toy_grammars.metta)

!(assertEqual
  (gparse:glr-forest-summary
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-g
    stmt
    (mm-cdecl-toks))
  (GParseGLRForestSummary Unique 5 16 11 1 5 0 7 4 13))

!(assertEqual
  (gparse:glr-forest-summary
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-send-nil-toks))
  (GParseGLRForestSummary Unique 8 19 11 1 8 0 4 7 13))

!(assertEqual
  (gparse:glr-forest-summary
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-nullable-g
    S
    (mkL (a c)))
  (GParseGLRForestSummary Unique 2 9 6 1 2 1 4 2 7))

!(assertEqual
  (gparse:glr-forest-summary
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-sss-g
    S
    (mkL (b b b)))
  (GParseGLRForestSummary Ambiguous 3 12 10 2 3 0 6 3 12))

!(assertEqual
  (gparse:glr-forest-summary
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-bad-toks))
  (GParseGLRForestSummary NoParse 7 3 0 0 3 0 0 0 4))
