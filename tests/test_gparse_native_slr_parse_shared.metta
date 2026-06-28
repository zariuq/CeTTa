!(import! &self lib_parse)
!(import! &self gparse)
!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self ./support/rhocalc_lib_parse_translator_v3.metta)

(= (g-bio) (mkGram (
   (VarD A base)(VarD U base)(VarD G base)(VarD C base)
   (Prod sone seq ((Hl base)))
   (Prod scons seq ((Hl base)(Hl seq))) )))

(= (g-lam) (mkGram (
   (VarD x term)(VarD y term)(VarD z term)
   (Prod lam term ((Tm LAM)(HlB term term)(Tm DOT)(Hl term))) )))

(= (g-bio-cert)
   (NodeC scons seq 0 3
     (Cons (LeafC A 0 1)
       (Cons (NodeC scons seq 1 3
           (Cons (LeafC U 1 2)
             (Cons (NodeC sone seq 2 3
               (Cons (LeafC G 2 3) Nil))
               Nil)))
         Nil))))

(= (g-lam-cert)
   (NodeC lam term 0 4
     (Cons (TokC LAM 0)
       (Cons (LeafC x 1 2)
         (Cons (TokC DOT 2)
           (Cons (LeafC x 3 4) Nil))))))

(= (mm-cdecl-cert)
   (NodeC st_c stmt 0 5
     (Cons (TokC MM_C 0)
       (Cons (NodeC sl2 sym_list 1 4
         (Cons (LeafC (Lex mm-sym wff) 1 2)
           (Cons (NodeC sl2 sym_list 2 4
             (Cons (LeafC (Lex mm-sym turn) 2 3)
               (Cons (NodeC sl1 sym_list 3 4
                 (Cons (LeafC (Lex mm-sym impl) 3 4) Nil))
                 Nil)))
             Nil)))
         (Cons (TokC MM_DOT 4) Nil)))))

(= (mm-empty-compressed-cert)
   (NodeC st_pc stmt 0 9
     (Cons (LeafC (Lex mm-label id0) 0 1)
       (Cons (TokC MM_P 1)
         (Cons (NodeC sl2 sym_list 2 4
           (Cons (LeafC (Lex mm-sym turn) 2 3)
             (Cons (NodeC sl1 sym_list 3 4
               (Cons (LeafC (Lex mm-sym ph) 3 4) Nil))
               Nil)))
           (Cons (TokC MM_EQPROOF 4)
             (Cons (TokC MM_LP 5)
               (Cons (NodeC ph0 proof_hdr_list 6 6 (Cons (EpsC) Nil))
                 (Cons (TokC MM_RP 6)
                   (Cons (NodeC cl1 cstep_list 7 8
                     (Cons (LeafC (Lex mm-cstep ABZ) 7 8) Nil))
                     (Cons (TokC MM_DOT 8) Nil)))))))))))

!(assertEqual
  (gparse:slr-parse-shared (gparse:repo-path "tests/test_gparse_native_slr_parse_shared.metta") g-bio seq (mkL (A U G)))
  (Unique (g-bio-cert)))

!(assertEqual
  (gparse:slr-parse-shared (gparse:repo-path "tests/test_gparse_native_slr_parse_shared.metta") g-lam term (mkL (LAM x DOT x)))
  (Unique (g-lam-cert)))

!(assertEqualToResult
  (gparse:slr-parse-shared (gparse:repo-path "tests/test_gparse_native_slr_parse_shared.metta") g-lam term (mkL (LAM x x)))
  (NoParse))

!(assertEqual
  (gparse:slr-parse-shared (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta") mm-g stmt (mm-cdecl-toks))
  (Unique (mm-cdecl-cert)))

!(assertEqual
  (gparse:slr-parse-shared (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta") mm-g stmt (mm-empty-compressed-thm-toks))
  (Unique (mm-empty-compressed-cert)))

!(assertEqualToResult
  (gparse:slr-parse-shared (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta") rho-g proc (rho-send-nil-toks))
  ((Error __cetta_lib_gparse_slr_parse_shared "SLR table has conflicts")))
