!(import! &self gparse)
!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self ./support/rhocalc_lib_parse_translator_v3.metta)
!(import! &self ./support/lib_parse_generalized_toy_grammars.metta)

(= (mm-min-found-toks-for-digest)
   (case (mm-lex-file (mm-mmtest-min-found-path))
     (((Ok $toks) $toks)
      ($o $o))))

!(assertEqual
  (gparse:dual-forest-signature-digest
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-g
    stmt
    (mm-cdecl-toks))
  (GParseDualForestSignatureDigest
    (GParseForestSignatureDigest Unique 5 1 12 "4d5edd79308821ee")
    (GParseForestSignatureDigest Unique 5 1 12 "4d5edd79308821ee")
    True))

!(assertEqual
  (gparse:dual-forest-signature-digest
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-send-nil-toks))
  (GParseDualForestSignatureDigest
    (GParseForestSignatureDigest Unique 8 1 12 "ec5d98e0cca8cff8")
    (GParseForestSignatureDigest Unique 8 1 12 "ec5d98e0cca8cff8")
    True))

!(assertEqual
  (gparse:dual-forest-signature-digest
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-bad-toks))
  (GParseDualForestSignatureDigest
    (GParseForestSignatureDigest NoParse 7 0 0 "732ab70654ae10e2")
    (GParseForestSignatureDigest NoParse 7 0 0 "732ab70654ae10e2")
    True))

!(assertEqual
  (gparse:dual-forest-signature-digest
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-nullable-g
    S
    (mkL (a c)))
  (GParseDualForestSignatureDigest
    (GParseForestSignatureDigest Unique 2 1 7 "a9f3430a04f5a96e")
    (GParseForestSignatureDigest Unique 2 1 7 "a9f3430a04f5a96e")
    True))

!(assertEqual
  (gparse:dual-forest-signature-digest
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-sss-g
    S
    (mkL (b b b)))
  (GParseDualForestSignatureDigest
    (GParseForestSignatureDigest Ambiguous 3 2 9 "1563be6e6a82f185")
    (GParseForestSignatureDigest Ambiguous 3 2 9 "1563be6e6a82f185")
    True))

!(assertEqual
  (gparse:dual-forest-signature-digest
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-db-g
    db
    (mm-min-found-toks-for-digest))
  (GParseDualForestSignatureDigest
    (GParseForestSignatureDigest Unique 75 1 170 "62eea4cc28ee0d4e")
    (GParseForestSignatureDigest Unique 75 1 170 "62eea4cc28ee0d4e")
    True))
