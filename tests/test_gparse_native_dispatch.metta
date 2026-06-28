!(import! &self lib_parse)
!(import! &self gparse)
!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self ./support/rhocalc_lib_parse_translator_v3.metta)
!(import! &self ./support/lib_parse_generalized_toy_grammars.metta)

!(assertEqual
  (gparse:backend-choice
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-g
    stmt)
  (GParseBackendChoice GLR (GParseSLRSummary 44 34 30 31 1 0)))

!(assertEqual
  (gparse:backend-choice
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc)
  (GParseBackendChoice GLR (GParseSLRSummary 39 67 14 95 1 1)))

!(assertEqual
  (gparse:backend-choice
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-sss-g
    S)
  (GParseBackendChoice GLR (GParseSLRSummary 5 3 6 5 1 1)))

!(assertEqual
  (gparse:parse-class
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-g
    stmt
    (mm-cdecl-toks))
  Unique)

!(assertEqual
  (case (gparse:parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert) (lp-shared-parse-cert (mm-g) stmt (mm-cdecl-toks) $cert))
     ($o False)))
  True)

!(assertEqual
  (gparse:parse-replay-report
    (mm-g)
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-g
    stmt
    (mm-cdecl-toks))
  (GParseReplay Unique (GParseCertShape 4 3 2 0 0 5) True))

!(assertEqual
  (case (gparse:parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (mm-g)
        stmt
        (mm-cdecl-toks)
        (lp-shared-cert-corrupt-first-token $cert)))
     ($o $o)))
  False)

!(assertEqual
  (gparse:parse-class
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-send-nil-toks))
  Unique)

!(assertEqual
  (case (gparse:parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert) (lp-shared-parse-cert (rho-g) proc (rho-send-nil-toks) $cert))
     ($o False)))
  True)

!(assertEqual
  (gparse:parse-replay-report
    (rho-g)
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-send-nil-toks))
  (GParseReplay Unique (GParseCertShape 4 0 8 0 0 8) True))

!(assertEqual
  (==
    (gparse:forest-summary
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (gparse:glr-forest-summary
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks)))
  True)

!(assertEqual
  (==
    (gparse:forest-signature
      (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
      toy-nullable-g
      S
      (mkL (a c)))
    (gparse:glr-forest-signature
      (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
      toy-nullable-g
      S
      (mkL (a c))))
  True)

!(assertEqual
  (gparse:packed-forest-shape
    (gparse:forest-data
      (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
      toy-sss-g
      S
      (mkL (b b b))))
  (GParsePackedForestShape Ambiguous 3 2 10 12))

!(assertEqual
  (gparse:parse-class
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-bad-toks))
  NoParse)

!(assertEqual
  (gparse:parse-replay-report
    (rho-g)
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-bad-toks))
  (GParseReplay NoParse))

!(assertEqual
  (gparse:parse-class
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g-ambig
    proc
    (mkL (x BAR x BAR x)))
  Ambiguous)

!(assertEqual
  (gparse:parse-replay-report
    (rho-g-ambig)
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g-ambig
    proc
    (mkL (x BAR x BAR x)))
  (GParseReplay Ambiguous))

!(assertEqual
  (gparse:forest-signature-digest
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-send-nil-toks))
  (GParseForestSignatureDigest Unique 8 1 12 "ec5d98e0cca8cff8"))
