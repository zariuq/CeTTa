!(import! &self gparse)
!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self ./support/rhocalc_lib_parse_translator_v3.metta)
!(import! &self ./support/lib_parse_generalized_toy_grammars.metta)

!(assertEqual
  (gparse:gll-forest-summary
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-g
    stmt
    (mm-cdecl-toks))
  (GParseGLLForestSummary Unique 5 22 17 1 5 0 10 7 67))

!(assertEqual
  (gparse:gll-forest-summary
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-send-nil-toks))
  (GParseGLLForestSummary Unique 8 22 14 1 8 0 4 10 69))

!(assertEqual
  (gparse:gll-forest-summary
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-nullable-g
    S
    (mkL (a c)))
  (GParseGLLForestSummary Unique 2 9 6 1 2 1 4 2 16))

!(assertEqual
  (gparse:gll-forest-summary
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-sss-g
    S
    (mkL (b b b)))
  (GParseGLLForestSummary Ambiguous 3 15 13 2 3 0 6 6 60))

!(assertEqual
  (gparse:gll-forest-summary
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-bad-toks))
  (GParseGLLForestSummary NoParse 7 8 5 0 3 0 1 4 37))
