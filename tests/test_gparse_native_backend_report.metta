!(import! &self gparse)
!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self ./support/rhocalc_lib_parse_translator_v3.metta)

!(assertEqual
  (gparse:backend-report
    (mm-g)
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-g
    stmt
    (mm-cdecl-toks))
  (GParseBackendReport
    (GParseBackendChoice GLR (GParseSLRSummary 44 34 30 31 1 0))
    (GParseSLRSummary 44 34 30 31 1 0)
    Unique
    (GParseReplay Unique (GParseCertShape 4 3 2 0 0 5) True)
    (GParseGLRForestSummary Unique 5 16 11 1 5 0 7 4 13)
    (GParseGLRForestSummary Unique 5 16 11 1 5 0 7 4 13)
    (GParseGLLForestSummary Unique 5 22 17 1 5 0 10 7 67)
    (GParseForestSignatureDigest Unique 5 1 12 "4d5edd79308821ee")
    (GParseDualForestSignatureDigest
      (GParseForestSignatureDigest Unique 5 1 12 "4d5edd79308821ee")
      (GParseForestSignatureDigest Unique 5 1 12 "4d5edd79308821ee")
      True)
    (GParseDualForestSignatureSummary
      (GParseForestSignatureSummary Unique 5 1 12)
      (GParseForestSignatureSummary Unique 5 1 12)
      True)
    (GParseDualUnique
      (GParseCertShape 4 3 2 0 0 5)
      (GParseCertShape 4 3 2 0 0 5)
      True
      True
      True
      True)))

!(assertEqual
  (gparse:backend-report
    (rho-g)
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-send-nil-toks))
  (GParseBackendReport
    (GParseBackendChoice GLR (GParseSLRSummary 39 67 14 95 1 1))
    (GParseSLRSummary 39 67 14 95 1 1)
    Unique
    (GParseReplay Unique (GParseCertShape 4 0 8 0 0 8) True)
    (GParseGLRForestSummary Unique 8 19 11 1 8 0 4 7 13)
    (GParseGLRForestSummary Unique 8 19 11 1 8 0 4 7 13)
    (GParseGLLForestSummary Unique 8 22 14 1 8 0 4 10 69)
    (GParseForestSignatureDigest Unique 8 1 12 "ec5d98e0cca8cff8")
    (GParseDualForestSignatureDigest
      (GParseForestSignatureDigest Unique 8 1 12 "ec5d98e0cca8cff8")
      (GParseForestSignatureDigest Unique 8 1 12 "ec5d98e0cca8cff8")
      True)
    (GParseDualForestSignatureSummary
      (GParseForestSignatureSummary Unique 8 1 12)
      (GParseForestSignatureSummary Unique 8 1 12)
      True)
    (GParseDualUnique
      (GParseCertShape 4 0 8 0 0 8)
      (GParseCertShape 4 0 8 0 0 8)
      True
      True
      True
      True)))

!(assertEqual
  (gparse:backend-report
    (rho-g)
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-bad-toks))
  (GParseBackendReport
    (GParseBackendChoice GLR (GParseSLRSummary 39 67 14 95 1 1))
    (GParseSLRSummary 39 67 14 95 1 1)
    NoParse
    (GParseReplay NoParse)
    (GParseGLRForestSummary NoParse 7 3 0 0 3 0 0 0 4)
    (GParseGLRForestSummary NoParse 7 3 0 0 3 0 0 0 4)
    (GParseGLLForestSummary NoParse 7 8 5 0 3 0 1 4 37)
    (GParseForestSignatureDigest NoParse 7 0 0 "732ab70654ae10e2")
    (GParseDualForestSignatureDigest
      (GParseForestSignatureDigest NoParse 7 0 0 "732ab70654ae10e2")
      (GParseForestSignatureDigest NoParse 7 0 0 "732ab70654ae10e2")
      True)
    (GParseDualForestSignatureSummary
      (GParseForestSignatureSummary NoParse 7 0 0)
      (GParseForestSignatureSummary NoParse 7 0 0)
      True)
    (GParseDualClass NoParse)))

!(assertEqual
  (gparse:backend-report
    (rho-g-ambig)
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g-ambig
    proc
    (mkL (x BAR x BAR x)))
  (GParseBackendReport
    (GParseBackendChoice GLR (GParseSLRSummary 5 4 2 3 1 1))
    (GParseSLRSummary 5 4 2 3 1 1)
    Ambiguous
    (GParseReplay Ambiguous)
    (GParseGLRForestSummary Ambiguous 5 17 13 2 5 0 6 6 15)
    (GParseGLRForestSummary Ambiguous 5 17 13 2 5 0 6 6 15)
    (GParseGLLForestSummary Ambiguous 5 20 16 2 5 0 6 9 48)
    (GParseForestSignatureDigest Ambiguous 5 2 11 "c71d742e07ee1657")
    (GParseDualForestSignatureDigest
      (GParseForestSignatureDigest Ambiguous 5 2 11 "c71d742e07ee1657")
      (GParseForestSignatureDigest Ambiguous 5 2 11 "c71d742e07ee1657")
      True)
    (GParseDualForestSignatureSummary
      (GParseForestSignatureSummary Ambiguous 5 2 11)
      (GParseForestSignatureSummary Ambiguous 5 2 11)
      True)
    (GParseDualClass Ambiguous)))
