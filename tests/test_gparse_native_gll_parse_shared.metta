!(import! &self gparse)
!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self ./support/rhocalc_lib_parse_translator_v3.metta)

!(assertEqual
  (gparse:gll-parse-shared
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-g
    stmt
    (mm-cdecl-toks))
  (Unique
    (NodeC st_c stmt 0 5
      (Cons (TokC MM_C 0)
        (Cons
          (NodeC sl2 sym_list 1 4
            (Cons (LeafC (Lex mm-sym wff) 1 2)
              (Cons
                (NodeC sl2 sym_list 2 4
                  (Cons (LeafC (Lex mm-sym turn) 2 3)
                    (Cons
                      (NodeC sl1 sym_list 3 4
                        (Cons (LeafC (Lex mm-sym impl) 3 4) Nil))
                      Nil)))
                Nil)))
          (Cons (TokC MM_DOT 4) Nil))))))

!(assertEqual
  (gparse:gll-parse-shared
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-send-nil-toks))
  (Unique
    (NodeC psend proc 0 8
      (Cons
        (NodeC nquot name 0 4
          (Cons (TokC AT 0)
            (Cons (TokC LBR 1)
              (Cons
                (NodeC pnil proc 2 3
                  (Cons (TokC ZERO 2) Nil))
                (Cons (TokC RBR 3) Nil)))))
        (Cons (TokC BANG 4)
          (Cons (TokC LP 5)
            (Cons
              (NodeC pnil proc 6 7
                (Cons (TokC ZERO 6) Nil))
              (Cons (TokC RP 7) Nil))))))))

!(assertEqual
  (gparse:gll-parse-shared
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-bad-toks))
  NoParse)

!(assertEqual
  (gparse:gll-parse-shared
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g-ambig
    proc
    (mkL (x BAR x BAR x)))
  Ambiguous)
