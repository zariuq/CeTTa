!(import! &self lib_parse)

(= (g-logic) (mkGram (
   (VarD t term)(VarD r term)
   (Prod weq wff ((Hl term)(Tm EQ)(Hl term)))
   (Prod wi  wff ((Tm LP)(Hl wff)(Tm IMP)(Hl wff)(Tm RP)))
   (Prod tpl term ((Tm LP)(Hl term)(Tm PLUS)(Hl term)(Tm RP)))
   (Prod tze term ((Tm ZERO))) )))

(= (g-bio) (mkGram (
   (VarD A base)(VarD U base)(VarD G base)(VarD C base)
   (Prod sone seq ((Hl base)))
   (Prod scons seq ((Hl base)(Hl seq))) )))

(= (g-tensor) (mkGram (
   (VarD a num)(VarD b num)(VarD c num)(VarD d num)
   (Prod inum item ((Hl num)))
   (Prod ivec item ((Hl vec)))
   (Prod e1 elts ((Hl item)))
   (Prod e2 elts ((Hl item)(Hl elts)))
   (Prod vec vec ((Tm LB)(Hl elts)(Tm RB))) )))

(= (g-ambig) (mkGram (
   (VarD a expr)(VarD b expr)(VarD c expr)
   (Prod eadd expr ((Hl expr)(Tm PLUS)(Hl expr))) )))

(= (g-juxt) (mkGram (
   (VarD f expr)(VarD x expr)
   (Prod app expr ((Hl expr)(Hl expr))) )))

(= (g-arith) (mkGram (
   (VarD a expr)(VarD b expr)(VarD c expr)
   (Prod eadd expr ((Hl expr)(Tm PLUS)(Hl expr)))
   (Prod emul expr ((Hl expr)(Tm STAR)(Hl expr))) )))

(= (g-lam) (mkGram (
   (VarD x term)(VarD y term)(VarD z term)
   (Prod lam term ((Tm LAM)(HlB term term)(Tm DOT)(Hl term))) )))

!(assertEqual (lp-parse (g-logic) wff (mkL (LP t EQ t IMP LP t PLUS ZERO RP EQ t RP)))
              (Unique (Nd wi (Cons (Nd weq (Cons (Lf t)(Cons (Lf t) Nil)))
                              (Cons (Nd weq (Cons (Nd tpl (Cons (Lf t)(Cons (Nd tze Nil) Nil)))
                                              (Cons (Lf t) Nil))) Nil)))))
!(assertEqual (lp-parse (g-bio) seq (mkL (A U G)))
              (Unique (Nd scons (Cons (Lf A)(Cons (Nd scons (Cons (Lf U)(Cons (Nd sone (Cons (Lf G) Nil)) Nil))) Nil)))))
!(assertEqual (lp-print (g-tensor)
                (pu-tree (lp-parse (g-tensor) vec (mkL (LB LB a RB LB b RB RB)))))
              (Ok (mkL (LB LB a RB LB b RB RB))))
!(assertEqual (lp-admit (g-ambig)) Admitted)
!(assertEqual (lp-parse (g-ambig) expr (mkL (a PLUS b PLUS c))) Ambiguous)
!(assertEqual (lp-parse-cert (g-bio) seq (mkL (A U G)) (Nd sone (Cons (Lf A) Nil))) False)
!(assertEqual (lp-admit BADGRAM) (Rejected bad-grammar))
!(assertEqual (lp-admit (Cons BAD Nil)) (Rejected bad-grammar))
!(assertEqual (lp-admit (mkGram ((Prod p wff ((Tm X)))(Prod p wff ((Tm Y)))))) (Rejected dup-label))
!(assertEqual (lp-admit (mkGram ((Prod p wff ((Tm X)))(Prod q wff ((Tm X)))))) (Rejected dup-surface))
!(assertEqual (lp-admit (mkGram ((VarD x term)(VarD x term)))) (Rejected dup-var))
!(assertEqual (lp-admit (mkGram ((Prod p wff ((Hl undef)))))) (Rejected undeclared-hole))
!(assertEqual (lp-admit (mkGram ((VarD x term)(Prod lam term ((Tm LAM)(HlB term undef)(Tm DOT)(Hl term)))))) (Rejected undeclared-hole))
!(assertEqual (lp-print (g-bio) (Nd nosuch (Cons (Lf A) Nil))) (Rejected bad-tree))
!(assertEqual (root-nt BADGRAM (Lf x)) (Err bad-grammar))
!(assertEqual (root-nt (Cons BAD Nil) (Lf x)) (Err bad-grammar))
!(assertEqual (tree-ok BADGRAM (Lf x)) False)
!(assertEqual (nd-ok BADGRAM nosuch Nil) False)
!(assertEqual (kids-ok BADGRAM (Cons term Nil) (Cons (Lf x) Nil)) False)
!(assertEqual (lp-print BADGRAM (Lf x)) (Rejected bad-grammar))
!(assertEqual (lp-parse-cert BADGRAM term (mkL (x)) (Lf x)) False)
!(assertEqual (lp-lower-cert BADGRAM (Lf x) (Lf x)) False)
!(assertEqual (lp-parse-cert BADGRAM term (mkL (x)) (Lf x) (Lf x)) False)

!(assertEqual (tree-ok (g-logic) (Nd NOSUCHLABEL Nil)) False)
!(assertEqual (root-nt (g-logic) (Nd NOSUCHLABEL Nil)) (Err noprod))
!(assertEqual (tree-ok (g-logic) (Lf nosuchvar)) False)
!(assertEqual (root-nt (g-logic) (Lf nosuchvar)) (Err novar))
!(assertEqual (nd-ok (g-bio) nosuch (Cons (Lf A) Nil)) False)
!(assertEqual (nd-ok (g-bio) sone Nil) False)
!(assertEqual (kids-ok (g-bio) (Cons base Nil) Nil) False)
!(assertEqual (kids-ok (g-bio) Nil (Cons (Lf A) Nil)) False)
!(assertEqual (kids-ok (g-logic) (Cons wff Nil) (Cons (Lf t) Nil)) False)
!(assertEqual (lp-print (g-logic) (Nd NOSUCHLABEL Nil)) (Rejected bad-tree))
!(assertEqual (lp-print (g-logic) (Lf nosuchvar)) (Rejected bad-tree))
!(assertEqual (lp-parse (g-juxt) expr (mkL (f x)))
              (Unique (Nd app (Cons (Lf f) (Cons (Lf x) Nil)))))
!(assertEqual (lp-parse (g-arith) expr (mkL (a PLUS b STAR c))) Ambiguous)
!(assertEqual (lp-parse (g-bio) seq Nil) NoParse)
!(assertEqual
  (lp-print (mkGram ((Prod p wff ((Tm X)))(Prod q wff ((Tm X))))) (Nd p Nil))
  (Rejected bad-grammar))
!(assertEqual
  (lp-parse-cert (mkGram ((Prod p wff ((Tm X)))(Prod q wff ((Tm X))))) wff (mkL (X)) (Nd p Nil))
  False)
!(assertEqual
  (lp-parse-cert
    (g-logic)
    term
    (mkL (LP t EQ t IMP LP t PLUS ZERO RP EQ t RP))
    (Nd wi
      (Cons (Nd weq (Cons (Lf t) (Cons (Lf t) Nil)))
        (Cons (Nd weq (Cons (Nd tpl (Cons (Lf t) (Cons (Nd tze Nil) Nil)))
                        (Cons (Lf t) Nil)))
          Nil))))
  False)
!(assertEqual
  (lp-parse-cert (g-logic) term (mkL (nosuchvar)) (Lf nosuchvar))
  False)

!(assertEqual
  (lp-parse-raw (g-lam) term (mkL (LAM x DOT x)))
  (Unique (Nd lam (Cons (Lf x) (Cons (Lf x) Nil)))))
!(assertEqual
  (lp-parse (g-lam) term (mkL (LAM x DOT x)))
  (Unique (Nd lam (Cons (Bd term term) (Cons (Bv 0) Nil)))))
!(assertEqual
  (lp-parse (g-lam) term (mkL (LAM y DOT y)))
  (Unique (Nd lam (Cons (Bd term term) (Cons (Bv 0) Nil)))))
!(assertEqual
  (== (lp-parse (g-lam) term (mkL (LAM x DOT x)))
      (lp-parse (g-lam) term (mkL (LAM y DOT y))))
  True)
!(assertEqual
  (lp-lower (g-lam)
    (Nd lam (Cons (Lf x)
      (Cons (Nd lam (Cons (Lf y) (Cons (Lf x) Nil))) Nil))))
  (Nd lam (Cons (Bd term term)
    (Cons (Nd lam (Cons (Bd term term) (Cons (Bv 1) Nil))) Nil))))
!(assertEqual
  (lp-lower (g-lam)
    (Nd lam (Cons (Lf x)
      (Cons (Nd lam (Cons (Lf x) (Cons (Lf x) Nil))) Nil))))
  (Nd lam (Cons (Bd term term)
    (Cons (Nd lam (Cons (Bd term term) (Cons (Bv 0) Nil))) Nil))))
!(assertEqual
  (lp-print (g-lam) (pu-tree (lp-parse (g-lam) term (mkL (LAM x DOT x)))))
  (Rejected bad-tree))
!(assertEqual
  (lp-parse-cert
    (g-lam)
    term
    (mkL (LAM x DOT x))
    (pu-tree (lp-parse (g-lam) term (mkL (LAM x DOT x)))))
  False)
!(assertEqual
  (lp-lower-cert
    (g-lam)
    (Nd lam (Cons (Lf x) (Cons (Lf x) Nil)))
    (Nd lam (Cons (Bd term term) (Cons (Bv 0) Nil))))
  True)
!(assertEqual
  (lp-lower-cert
    (g-lam)
    (Nd lam (Cons (Lf x) (Cons (Lf x) Nil)))
    (Nd lam (Cons (Bd term term) (Cons (Lf x) Nil))))
  False)
!(assertEqual
  (lp-parse-cert
    (g-lam)
    term
    (mkL (LAM x DOT x))
    (Nd lam (Cons (Lf x) (Cons (Lf x) Nil)))
    (Nd lam (Cons (Bd term term) (Cons (Bv 0) Nil))))
  True)
!(assertEqual
  (lp-parse-cert
    (g-lam)
    term
    (mkL (LAM x DOT x))
    (Nd lam (Cons (Lf x) (Cons (Lf x) Nil)))
    (Nd lam (Cons (Bd term term) (Cons (Lf x) Nil))))
  False)
