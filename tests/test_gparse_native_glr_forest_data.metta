!(import! &self gparse)
!(import! &self ./support/lib_parse_generalized_toy_grammars.metta)

!(assertEqual
  (gparse:glr-forest-data
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-nullable-g
    S
    (mkL (a c)))
  (GParsePackedForest Unique 2 1 8
    (Cons (GNodeTerm 0 a 0 1)
      (Cons (GNodeSym 1 a_tok 0 1 (Cons (GChoice GNone 0 0 3) Nil))
        (Cons (GNodeSym 2 B 1 1 (Cons (GChoice GNone 3 1 2) Nil))
          (Cons (GNodeEps 3 1 1)
            (Cons (GNodeTerm 4 c 1 2)
              (Cons (GNodeSym 5 c_tok 1 2 (Cons (GChoice GNone 4 1 5) Nil))
                (Cons (GNodeInter 6 0 1 0 1 (Cons (GChoice GNone 1 0 -1) Nil))
                  (Cons (GNodeInter 7 0 2 0 1 (Cons (GChoice 6 2 1 -1) Nil))
                    (Cons (GNodeSym 8 S 0 2 (Cons (GChoice 7 5 1 0) Nil))
                      Nil)))))))))))

!(assertEqual
  (gparse:glr-forest-data
    (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta")
    toy-sss-g
    S
    (mkL (b b b)))
  (GParsePackedForest Ambiguous 3 2 10
    (Cons (GNodeTerm 0 b 0 1)
      (Cons (GNodeSym 1 S 0 1 (Cons (GChoice GNone 0 0 2) Nil))
        (Cons (GNodeTerm 2 b 1 2)
          (Cons (GNodeSym 3 S 1 2 (Cons (GChoice GNone 2 1 2) Nil))
            (Cons (GNodeInter 4 0 1 0 1 (Cons (GChoice GNone 1 0 -1) Nil))
              (Cons (GNodeSym 5 S 0 2 (Cons (GChoice 4 3 1 0) Nil))
                (Cons (GNodeTerm 6 b 2 3)
                  (Cons (GNodeSym 7 S 2 3 (Cons (GChoice GNone 6 2 2) Nil))
                    (Cons (GNodeInter 8 0 1 1 2 (Cons (GChoice GNone 3 1 -1) Nil))
                      (Cons (GNodeSym 9 S 1 3 (Cons (GChoice 8 7 2 0) Nil))
                        (Cons
                          (GNodeSym 10 S 0 3
                            (Cons (GChoice 4 9 1 0)
                              (Cons (GChoice 11 7 2 0) Nil)))
                          (Cons (GNodeInter 11 0 1 0 2 (Cons (GChoice GNone 5 0 -1) Nil))
                            Nil))))))))))))))
