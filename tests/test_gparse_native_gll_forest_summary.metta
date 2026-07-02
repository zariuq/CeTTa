!(import! &self gparse)
!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self ./support/rhocalc_lib_parse_translator_v3.metta)
!(import! &self ./support/lib_parse_generalized_toy_grammars.metta)

(= (test-gll-recognize $path $def $start $toks)
   (function
     (case (__cetta_lib_gparse_gll_recognize $path $def $start $toks)
       (($result (return $result))))))

(= (test-gll-recognize-token-file $path $def $start $token-path)
   (function
     (case (__cetta_lib_gparse_gll_recognize_token_file
             $path $def $start $token-path)
       (($result (return $result))))))

(= (test-gll-span-summary $path $def $start $toks)
   (function
     (case (__cetta_lib_gparse_gll_span_summary $path $def $start $toks)
       (($result (return $result))))))

(= (test-gll-span-summary-token-file $path $def $start $token-path)
   (function
     (case (__cetta_lib_gparse_gll_span_summary_token_file
             $path $def $start $token-path)
       (($result (return $result))))))

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

!(assertEqual
  (gparse:gll-forest-summary-token-file
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-plus-g
    E
    (gparse:repo-path "tests/support/gparse_toy_plus.tokens.tsv"))
  (gparse:gll-forest-summary
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-plus-g
    E
    (mkL (x PLUS x PLUS x))))

!(assertEqual
  (gparse:gll-forest-signature-digest-token-file
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-plus-g
    E
    (gparse:repo-path "tests/support/gparse_toy_plus.tokens.tsv"))
  (gparse:gll-forest-signature-digest
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-plus-g
    E
    (mkL (x PLUS x PLUS x))))

!(assertEqual
  (gparse:gll-forest-summary-token-file
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-lex-g
    S
    (gparse:repo-path "tests/support/gparse_toy_lex.tokens.tsv"))
  (gparse:gll-forest-summary
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-lex-g
    S
    (Cons (Lex WORD alpha)
      (Cons (Lex WORD "beta gamma") Nil))))

!(assertEqual
  (gparse:gll-forest-summary-token-file
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-plus-g
    E
    (gparse:repo-path "tests/support/gparse_bad.tokens.tsv"))
  ((Error __cetta_lib_gparse_gll_forest_summary_token_file
     "unknown token-file line 1")))

!(assertEqual
  (test-gll-span-summary
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-g
    stmt
    (mm-cdecl-toks))
  (GParseGLLSpanSummary Accepted 5 10 18 49))

!(assertEqual
  (test-gll-span-summary
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-sss-g
    S
    (mkL (b b b)))
  (GParseGLLSpanSummary Accepted 3 6 12 48))

!(assertEqual
  (test-gll-span-summary
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-bad-toks))
  (GParseGLLSpanSummary Rejected 7 1 6 31))

!(assertEqual
  (test-gll-span-summary-token-file
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-plus-g
    E
    (gparse:repo-path "tests/support/gparse_toy_plus.tokens.tsv"))
  (test-gll-span-summary
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-plus-g
    E
    (mkL (x PLUS x PLUS x))))

!(assertEqual
  (test-gll-span-summary-token-file
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-plus-g
    E
    (gparse:repo-path "tests/support/gparse_bad.tokens.tsv"))
  ((Error __cetta_lib_gparse_gll_span_summary_token_file
     "unknown token-file line 1")))

!(assertEqual
  (test-gll-recognize
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-plus-g
    E
    (mkL (x PLUS x PLUS x)))
  (GParseGLLRecognize Accepted 5 8 29))

!(assertEqual
  (test-gll-recognize-token-file
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-plus-g
    E
    (gparse:repo-path "tests/support/gparse_toy_plus.tokens.tsv"))
  (GParseGLLRecognize Accepted 5 8 29))

!(assertEqual
  (test-gll-recognize
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-plus-g
    E
    (mkL (x PLUS PLUS)))
  (GParseGLLRecognize Rejected 3 6 15))

!(assertEqual
  (test-gll-recognize-token-file
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-plus-g
    E
    (gparse:repo-path "tests/support/gparse_bad.tokens.tsv"))
  ((Error __cetta_lib_gparse_gll_recognize_token_file
     "unknown token-file line 1")))
