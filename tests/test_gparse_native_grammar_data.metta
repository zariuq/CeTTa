!(import! &self gparse)

(= (g-bio) (mkGram (
   (VarD A base)(VarD U base)(VarD G base)(VarD C base)
   (Prod sone seq ((Hl base)))
   (Prod scons seq ((Hl base)(Hl seq))) )))

(= (g-lam) (mkGram (
   (LexD IDENT name)
   (VarD LAM kw)(VarD DOT dot)
   (Prod pname term ((Hl name)))
   (Prod lam term ((Hl kw)(HlB name term)(Hl dot)(Hl term))) )))

!(assertEqual
  (gparse:grammar-data (gparse:repo-path "tests/test_gparse_native_grammar_data.metta") g-bio)
  (GParseGrammarData
    Nil
    (Cons (VarD A base)
      (Cons (VarD U base)
        (Cons (VarD G base)
          (Cons (VarD C base) Nil))))
    (Cons (Prod sone seq (Cons (Hl base) Nil))
      (Cons (Prod scons seq (Cons (Hl base) (Cons (Hl seq) Nil))) Nil))))

!(assertEqual
  (gparse:grammar-data (gparse:repo-path "tests/test_gparse_native_grammar_data.metta") g-lam)
  (GParseGrammarData
    (Cons (LexD IDENT name) Nil)
    (Cons (VarD LAM kw)
      (Cons (VarD DOT dot) Nil))
    (Cons (Prod pname term (Cons (Hl name) Nil))
        (Cons (Prod lam term
          (Cons (Hl kw)
            (Cons (HlB name term)
              (Cons (Hl dot)
                (Cons (Hl term) Nil)))))
        Nil))))

!(assertEqualToResult
  (gparse:grammar-data (gparse:repo-path "tests/test_gparse_native_grammar_data.metta") nope)
  ((Error __cetta_lib_gparse_grammar_data "definition nope not found")))
