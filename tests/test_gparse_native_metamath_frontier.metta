!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self gparse)

(= (mm-frontier-class $path)
   (case (mm-lex-file $path)
     (((Ok $toks)
       (gparse:parse-class
         (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
         mm-db-g db $toks))
      ($o $o))))

!(assertEqual
  (gparse:backend-choice
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-db-g db)
  (GParseBackendChoice GLR (GParseSLRSummary 43 44 40 39 1 0)))

!(assertEqual
  (mm-frontier-class (mm-mmtest-min-found-path))
  Unique)

!(assertEqual
  (mm-frontier-class "../metamath/metamath-test/tests/unit/test_dv_pairwise.mm")
  Unique)

!(assertEqual
  (mm-frontier-class "../metamath/metamath-test/tests/mmverify/test_nested_scope_exact.mm")
  Unique)

!(assertEqual
  (mm-frontier-class "../metamath/metamath-test/tests/mmverify/test_nested_scope_proof.mm")
  Unique)
