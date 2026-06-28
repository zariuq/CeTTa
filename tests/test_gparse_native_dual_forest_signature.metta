!(import! &self gparse)
!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self ./support/rhocalc_lib_parse_translator_v3.metta)
!(import! &self ./support/lib_parse_generalized_toy_grammars.metta)

!(assertEqual
  (gparse:dual-forest-signature-summary
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-g
    stmt
    (mm-cdecl-toks))
  (GParseDualForestSignatureSummary
    (GParseForestSignatureSummary Unique 5 1 12)
    (GParseForestSignatureSummary Unique 5 1 12)
    True))

!(assertEqual
  (gparse:dual-forest-signature-summary
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-send-nil-toks))
  (GParseDualForestSignatureSummary
    (GParseForestSignatureSummary Unique 8 1 12)
    (GParseForestSignatureSummary Unique 8 1 12)
    True))

!(assertEqual
  (gparse:dual-forest-signature-summary
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-nullable-g
    S
    (mkL (a c)))
  (GParseDualForestSignatureSummary
    (GParseForestSignatureSummary Unique 2 1 7)
    (GParseForestSignatureSummary Unique 2 1 7)
    True))

!(assertEqual
  (gparse:dual-forest-signature-summary
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-sss-g
    S
    (mkL (b b b)))
  (GParseDualForestSignatureSummary
    (GParseForestSignatureSummary Ambiguous 3 2 9)
    (GParseForestSignatureSummary Ambiguous 3 2 9)
    True))

!(assertEqual
  (gparse:dual-forest-signature-summary
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-bad-toks))
  (GParseDualForestSignatureSummary
    (GParseForestSignatureSummary NoParse 7 0 0)
    (GParseForestSignatureSummary NoParse 7 0 0)
    True))
