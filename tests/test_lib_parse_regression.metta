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

(= (g-lam-tpl) (template-of (g-lam) lam))
(= (g-lam-tpl-len) (tpl-len (g-lam-tpl)))
(= (g-lam-tpl-first) (tpl-nth (g-lam-tpl) 0))
(= (g-lam-tpl-dot) (tpl-nth (g-lam-tpl) 2))
(= (g-lam-tpl-hlb-ok) (tpl-nth-is-hlb (g-lam-tpl) 1 term term))
(= (g-lam-tpl-hl-ok) (tpl-nth-is-hl (g-lam-tpl) 3 term))
(= (g-lam-tpl-hl-count) (tpl-count-hl (g-lam-tpl) term))

(= (g-rho-gap) (mkGram (
   (VarD x rho_id)(VarD y rho_id)
   (Prod pnil  proc ((Tm ZERO)))
   (Prod pdrop proc ((Tm STAR)(Hl name)))
   (Prod precv proc ((Tm FOR)(Tm LP)(Tm DOLLAR)(HlB rho_id proc)(Tm FROM)(Hl name)(Tm RP)(Tm LBR)(Hl proc)(Tm RBR)))
   (Prod nquot name ((Tm AT)(Tm LBR)(Hl proc)(Tm RBR)))
   (Prod nvar  name ((Tm DOLLAR)(Hl rho_id))) )))

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
!(assertEqual (lp-parse-class (g-bio) seq (mkL (A U G))) Unique)
!(assertEqual (lp-parse-class (g-ambig) expr (mkL (a PLUS b PLUS c))) Ambiguous)
!(assertEqual (lp-parse-class (g-bio) seq Nil) NoParse)
!(println!
  (lp-parse-cert
    (g-logic)
    term
    (mkL (LP t EQ t IMP LP t PLUS ZERO RP EQ t RP))
    (Nd wi
      (Cons (Nd weq (Cons (Lf t) (Cons (Lf t) Nil)))
        (Cons (Nd weq (Cons (Nd tpl (Cons (Lf t) (Cons (Nd tze Nil) Nil)))
                        (Cons (Lf t) Nil)))
          Nil)))))
!(assertEqual (g-lam-tpl-len) 4)
!(assertEqual (g-lam-tpl-first) (Tm LAM))
!(assertEqual (g-lam-tpl-dot) (Tm DOT))
!(assertEqual (g-lam-tpl-hlb-ok) True)
!(assertEqual (g-lam-tpl-hl-ok) True)
!(assertEqual (g-lam-tpl-hl-count) 1)
!(assertEqual (tpl-len BAD) (Err badtpl))
!(assertEqual (tpl-nth BAD 0) (Err badtpl))
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
  (lp-parse-cert (g-logic) term (mkL (nosuchvar)) (Lf nosuchvar))
  False)
