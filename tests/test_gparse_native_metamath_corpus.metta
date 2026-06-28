!(import! &self lib_parse)
!(import! &self gparse)
!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

(= (mm-min-found-toks)
   (case (mm-lex-file (mm-mmtest-min-found-path))
     (((Ok $toks) $toks)
      ($o $o))))

!(assertEqual
  (mm-count (mm-min-found-toks))
  75)

!(assertEqual
  (gparse:dual-forest-signature-summary
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-db-g
    db
    (mm-min-found-toks))
  (GParseDualForestSignatureSummary
    (GParseForestSignatureSummary Unique 75 1 170)
    (GParseForestSignatureSummary Unique 75 1 170)
    True))

!(assertEqual
  (gparse:dual-parse-compare
    (mm-db-g)
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-db-g
    db
    (mm-min-found-toks))
  (GParseDualUnique
    (GParseCertShape 56 37 38 2 0 75)
    (GParseCertShape 56 37 38 2 0 75)
    True
    True
    True
    True))

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-db-g
      db
      (mm-min-found-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (mm-db-g)
        db
        (mm-min-found-toks)
        (lp-shared-cert-corrupt-first-token $cert)))
     ($o $o)))
  False)

!(assertEqual
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-db-g
      db
      (mm-min-found-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (mm-db-g)
        db
        (mm-min-found-toks)
        (lp-shared-cert-corrupt-first-token $cert)))
     ($o $o)))
  False)
