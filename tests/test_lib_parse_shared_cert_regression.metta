!(import! &self lib_parse)

(= (g-bio) (mkGram (
   (VarD A base)(VarD U base)(VarD G base)(VarD C base)
   (Prod sone seq ((Hl base)))
   (Prod scons seq ((Hl base)(Hl seq))) )))

(= (g-lam) (mkGram (
   (VarD x term)(VarD y term)(VarD z term)
   (Prod lam term ((Tm LAM)(HlB term term)(Tm DOT)(Hl term))) )))

(= (g-ambig) (mkGram (
   (VarD a atom)(VarD b atom)(VarD c atom)
   (Prod plus expr ((Hl expr)(Tm PLUS)(Hl expr)))
   (Prod lift expr ((Hl atom))) )))

(= (bio-cert)
   (NodeC scons seq 0 3
     (Cons (LeafC A 0 1)
       (Cons (NodeC scons seq 1 3
         (Cons (LeafC U 1 2)
           (Cons (NodeC sone seq 2 3
             (Cons (LeafC G 2 3) Nil))
             Nil)))
         Nil))))

(= (bio-bad-leaf-cert)
   (NodeC scons seq 0 3
     (Cons (LeafC A 0 1)
       (Cons (NodeC scons seq 1 3
         (Cons (LeafC U 1 2)
           (Cons (NodeC sone seq 2 3
             (Cons (LeafC C 2 3) Nil))
             Nil)))
         Nil))))

(= (bio-bad-span-cert)
   (NodeC scons seq 0 2
     (Cons (LeafC A 0 1)
       (Cons (NodeC scons seq 1 3
         (Cons (LeafC U 1 2)
           (Cons (NodeC sone seq 2 3
             (Cons (LeafC G 2 3) Nil))
             Nil)))
         Nil))))

(= (lam-cert)
   (NodeC lam term 0 4
     (Cons (TokC LAM 0)
       (Cons (LeafC x 1 2)
         (Cons (TokC DOT 2)
           (Cons (LeafC x 3 4) Nil))))))

(= (lam-bad-right-cert)
   (NodeC lam term 0 3
     (Cons (TokC LAM 0)
       (Cons (LeafC x 1 2)
         (Cons (TokC DOT 2)
           (Cons (LeafC x 3 4) Nil))))))

!(assertEqual
  (lp-shared-cert-tree (g-bio) seq (mkL (A U G)) (bio-cert))
  (Ok (Nd scons (Cons (Lf A) (Cons (Nd scons (Cons (Lf U) (Cons (Nd sone (Cons (Lf G) Nil)) Nil))) Nil)))))
!(assertEqual
  (lp-tree-shared-cert
    (g-bio)
    seq
    (mkL (A U G))
    (Nd scons (Cons (Lf A) (Cons (Nd scons (Cons (Lf U) (Cons (Nd sone (Cons (Lf G) Nil)) Nil))) Nil))))
  (Ok (bio-cert)))
!(assertEqual
  (lp-shared-parse-cert (g-bio) seq (mkL (A U G)) (bio-cert))
  True)
!(assertEqual
  (lp-shared-parse-cert (g-bio) seq (mkL (A U G)) (bio-bad-leaf-cert))
  False)
!(assertEqual
  (lp-shared-parse-cert
    (g-bio)
    seq
    (mkL (A U G))
    (lp-shared-cert-corrupt-first-token (bio-cert)))
  False)
!(assertEqual
  (lp-shared-cert-tree (g-bio) seq (mkL (A U G)) (bio-bad-span-cert))
  (Err bad-cert))
!(assertEqual
  (lp-shared-cert-tree (g-lam) term (mkL (LAM x DOT x)) (lam-cert))
  (Ok (Nd lam (Cons (Lf x) (Cons (Lf x) Nil)))))
!(assertEqual
  (lp-tree-shared-cert
    (g-lam)
    term
    (mkL (LAM x DOT x))
    (Nd lam (Cons (Lf x) (Cons (Lf x) Nil))))
  (Ok (lam-cert)))
!(assertEqual
  (lp-shared-parse-cert (g-lam) term (mkL (LAM x DOT x)) (lam-cert))
  True)
!(assertEqual
  (lp-shared-parse-cert
    (g-lam)
    term
    (mkL (LAM x DOT x))
    (lp-shared-cert-corrupt-first-token (lam-cert)))
  False)
!(assertEqual
  (lp-shared-parse-cert
    (g-lam)
    term
    (mkL (LAM x DOT x))
    (lam-cert)
    (Nd lam (Cons (Bd term term) (Cons (Bv 0) Nil))))
  True)
!(assertEqual
  (lp-shared-parse-cert
    (g-lam)
    term
    (mkL (LAM x DOT x))
    (lam-cert)
    (Nd lam (Cons (Bd term term) (Cons (Lf x) Nil))))
  False)
!(assertEqual
  (lp-shared-parse-cert
    (g-lam)
    term
    (mkL (LAM x DOT x))
    (lam-bad-right-cert))
  False)
!(assertEqual
  (lp-tree-shared-cert
    (g-lam)
    term
    (mkL (LAM x DOT x))
    (Nd lam (Cons (Lf x) Nil)))
  (Err bad-tree))
!(assertEqual
  (lp-parse-shared (g-bio) seq (mkL (A U G)))
  (Unique (bio-cert)))
!(assertEqual
  (lp-parse-shared (g-lam) term (mkL (LAM x DOT x)))
  (Unique (lam-cert)))
!(assertEqual
  (lp-parse-shared (g-ambig) expr (mkL (a PLUS b PLUS c)))
  Ambiguous)
